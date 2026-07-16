/*
 * Copyright 2026 nyan<(nyan4)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "interpreter.h"
#include "value.h"
#include "env.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Interpreter state and control-flow signalling                       */
/* ------------------------------------------------------------------ */

typedef enum { FLOW_NORMAL, FLOW_BREAK, FLOW_CONTINUE } Flow;

typedef struct {
    Env    *global;
    jmp_buf on_error;   /* longjmp target for runtime errors */
} Interp;

static void runtime_error(Interp *it, int line, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "myon: runtime error at line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    longjmp(it->on_error, 1);
}

static Value eval_expr(Interp *it, Env *env, Expr *e);
static Flow  exec_block(Interp *it, Env *env, StmtList *body);

/* ------------------------------------------------------------------ */
/* Casts / value constructors (spec 2.3)                               */
/* ------------------------------------------------------------------ */

static Value cast_to_str(Interp *it, int line, Value v) {
    (void)it; (void)line;
    char *s = value_to_cstr(&v);
    value_free(&v);
    return value_str(s);
}

static Value cast_to_int(Interp *it, int line, Value v) {
    switch (v.type) {
        case TYPE_INT: return v;
        case TYPE_FLOAT: { long long r = (long long)v.as.f; return value_int(r); }
        case TYPE_BOOL: { long long r = v.as.b; value_free(&v); return value_int(r); }
        case TYPE_STR: {
            char *end = NULL;
            long long r = strtoll(v.as.s, &end, 10);
            if (end == v.as.s || *end != '\0')
                runtime_error(it, line, "int(): cannot parse '%s' as integer", v.as.s);
            value_free(&v);
            return value_int(r);
        }
        case TYPE_CHAR: {
            /* first byte's codepoint (ASCII path) */
            long long r = (unsigned char)v.as.s[0];
            value_free(&v);
            return value_int(r);
        }
        default:
            runtime_error(it, line, "int(): unsupported source type %s", type_name(v.type));
            return value_nil();
    }
}

static Value cast_to_char(Interp *it, int line, Value v) {
    switch (v.type) {
        case TYPE_CHAR: return v;
        case TYPE_INT: {
            /* interpret as ASCII/first-byte codepoint */
            char buf[2] = { (char)(v.as.i & 0xFF), '\0' };
            value_free(&v);
            return value_char(myon_strdup(buf));
        }
        case TYPE_STR: {
            if (strlen(v.as.s) == 0)
                runtime_error(it, line, "char(): empty string");
            /* keep exactly one (byte) character for the ASCII subset */
            char buf[2] = { v.as.s[0], '\0' };
            value_free(&v);
            return value_char(myon_strdup(buf));
        }
        default:
            runtime_error(it, line, "char(): unsupported source type %s", type_name(v.type));
            return value_nil();
    }
}

/* ------------------------------------------------------------------ */
/* Arithmetic / comparison with strict typing (spec 2.2)               */
/* ------------------------------------------------------------------ */

/* checked integer arithmetic (spec 0.2: overflow is a runtime error) */
static long long int_arith(Interp *it, int line, OpKind op, long long a, long long b) {
    long long r;
    switch (op) {
        case OP_ADD:
            if (__builtin_add_overflow(a, b, &r))
                runtime_error(it, line, "integer overflow in addition");
            return r;
        case OP_SUB:
            if (__builtin_sub_overflow(a, b, &r))
                runtime_error(it, line, "integer overflow in subtraction");
            return r;
        case OP_MUL:
            if (__builtin_mul_overflow(a, b, &r))
                runtime_error(it, line, "integer overflow in multiplication");
            return r;
        case OP_DIV:
            if (b == 0) runtime_error(it, line, "division by zero");
            if (a == LLONG_MIN && b == -1)
                runtime_error(it, line, "integer overflow in division");
            return a / b;
        default:
            runtime_error(it, line, "unsupported integer operator");
            return 0;
    }
}

static int compare_ordered(double a, double b, OpKind op) {
    switch (op) {
        case OP_LT: return a < b;
        case OP_GT: return a > b;
        case OP_LE: return a <= b;
        case OP_GE: return a >= b;
        case OP_EQ: return a == b;
        case OP_NEQ:return a != b;
        default:    return 0;
    }
}

static Value eval_binary(Interp *it, int line, OpKind op, Value l, Value r) {
    /* equality against myon.nil is allowed for the error type (spec 2.4/6.2) */
    if (op == OP_EQ || op == OP_NEQ) {
        if (l.type == TYPE_NIL || r.type == TYPE_NIL) {
            int both_nil = (l.type == TYPE_NIL && r.type == TYPE_NIL);
            int one_err_nil =
                (l.type == TYPE_ERROR && r.type == TYPE_NIL) ||
                (r.type == TYPE_ERROR && l.type == TYPE_NIL);
            if (!both_nil && !one_err_nil)
                runtime_error(it, line,
                    "myon.nil may only be compared with error values (spec 2.4)");
            int equal = both_nil; /* an error != nil */
            value_free(&l); value_free(&r);
            return value_bool(op == OP_EQ ? equal : !equal);
        }
    }

    /* strict typing: operands must share a type */
    if (!type_equal(l.type, r.type)) {
        Type lt = l.type, rt = r.type;
        value_free(&l); value_free(&r);
        runtime_error(it, line,
            "type mismatch: cannot apply operator to %s and %s (strict typing, spec 2.2)",
            type_name(lt), type_name(rt));
    }

    switch (l.type) {
        case TYPE_INT: {
            long long a = l.as.i, b = r.as.i;
            switch (op) {
                case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
                    return value_int(int_arith(it, line, op, a, b));
                case OP_EQ: case OP_NEQ: case OP_LT:
                case OP_GT: case OP_LE: case OP_GE:
                    return value_bool(compare_ordered((double)a, (double)b, op));
                default: break;
            }
            break;
        }
        case TYPE_FLOAT: {
            double a = l.as.f, b = r.as.f;
            switch (op) {
                case OP_ADD: return value_float(a + b);
                case OP_SUB: return value_float(a - b);
                case OP_MUL: return value_float(a * b);
                case OP_DIV:
                    if (b == 0.0) runtime_error(it, line, "division by zero");
                    return value_float(a / b);
                case OP_EQ: case OP_NEQ: case OP_LT:
                case OP_GT: case OP_LE: case OP_GE:
                    return value_bool(compare_ordered(a, b, op));
                default: break;
            }
            break;
        }
        case TYPE_STR: {
            if (op == OP_ADD) {
                size_t la = strlen(l.as.s), lb = strlen(r.as.s);
                char *buf = (char *)myon_xmalloc(la + lb + 1);
                memcpy(buf, l.as.s, la);
                memcpy(buf + la, r.as.s, lb);
                buf[la + lb] = '\0';
                value_free(&l); value_free(&r);
                return value_str(buf);
            }
            if (op == OP_EQ || op == OP_NEQ) {
                int eq = strcmp(l.as.s, r.as.s) == 0;
                value_free(&l); value_free(&r);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            runtime_error(it, line, "unsupported operator on str");
            break;
        }
        case TYPE_CHAR: {
            if (op == OP_EQ || op == OP_NEQ) {
                int eq = strcmp(l.as.s, r.as.s) == 0;
                value_free(&l); value_free(&r);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            runtime_error(it, line, "unsupported operator on char");
            break;
        }
        case TYPE_BOOL: {
            if (op == OP_EQ || op == OP_NEQ) {
                int eq = (l.as.b == r.as.b);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            runtime_error(it, line, "unsupported operator on bool");
            break;
        }
        default:
            runtime_error(it, line, "unsupported operand type %s", type_name(l.type));
    }
    return value_nil();
}

/* ------------------------------------------------------------------ */
/* myon.print builtin (spec section 10)                                */
/* ------------------------------------------------------------------ */

static Value builtin_print(Interp *it, Env *env, Expr *call) {
    for (int i = 0; i < call->as.call.arg_count; i++) {
        if (call->as.call.arg_names[i])
            runtime_error(it, call->line, "myon.print does not accept named arguments");
        Value v = eval_expr(it, env, call->as.call.args[i]);
        char *s = value_to_cstr(&v);
        fputs(s, stdout);
        free(s);
        value_free(&v);
    }
    fputc('\n', stdout);
    return value_void();
}

/* ------------------------------------------------------------------ */
/* Expression evaluation                                               */
/* ------------------------------------------------------------------ */

static Value eval_expr(Interp *it, Env *env, Expr *e) {
    switch (e->kind) {
        case EXPR_INT_LIT:   return value_int(e->as.int_val);
        case EXPR_FLOAT_LIT: return value_float(e->as.float_val);
        case EXPR_BOOL_LIT:  return value_bool(e->as.bool_val);
        case EXPR_STRING:    return value_str(myon_strdup(e->as.str_val));
        case EXPR_NIL:       return value_nil();

        case EXPR_IDENT: {
            Value v;
            if (!env_get(env, e->as.ident, &v))
                runtime_error(it, e->line, "undefined variable '%s'", e->as.ident);
            return v;
        }

        case EXPR_STR_CTOR:
            return cast_to_str(it, e->line, eval_expr(it, env, e->as.operand));
        case EXPR_INT_CTOR:
            return cast_to_int(it, e->line, eval_expr(it, env, e->as.operand));
        case EXPR_CHAR_CTOR:
            return cast_to_char(it, e->line, eval_expr(it, env, e->as.operand));
        case EXPR_ERROR_CTOR: {
            Value v = eval_expr(it, env, e->as.operand);
            if (v.type != TYPE_STR)
                runtime_error(it, e->line, "error() expects a str argument");
            char *msg = myon_strdup(v.as.s);
            value_free(&v);
            return value_error(msg);
        }

        case EXPR_UNARY: {
            Value v = eval_expr(it, env, e->as.unary.operand);
            if (e->as.unary.op == OP_NEG) {
                if (v.type == TYPE_INT)   { long long r = -v.as.i; return value_int(r); }
                if (v.type == TYPE_FLOAT) { double r = -v.as.f; return value_float(r); }
                Type t = v.type; value_free(&v);
                runtime_error(it, e->line, "unary '-' requires int or float, got %s", type_name(t));
            } else { /* OP_NOT */
                if (v.type != TYPE_BOOL) {
                    Type t = v.type; value_free(&v);
                    runtime_error(it, e->line, "myon.not requires bool, got %s", type_name(t));
                }
                int r = !v.as.b;
                return value_bool(r);
            }
            return value_nil();
        }

        case EXPR_LOGICAL: {
            Value l = eval_expr(it, env, e->as.binary.left);
            if (l.type != TYPE_BOOL) {
                Type t = l.type; value_free(&l);
                runtime_error(it, e->line, "logical operator requires bool, got %s", type_name(t));
            }
            /* short-circuit */
            if (e->as.binary.op == OP_AND && !l.as.b) return value_bool(0);
            if (e->as.binary.op == OP_OR  &&  l.as.b) return value_bool(1);
            Value r = eval_expr(it, env, e->as.binary.right);
            if (r.type != TYPE_BOOL) {
                Type t = r.type; value_free(&r);
                runtime_error(it, e->line, "logical operator requires bool, got %s", type_name(t));
            }
            return value_bool(r.as.b);
        }

        case EXPR_BINARY: {
            Value l = eval_expr(it, env, e->as.binary.left);
            Value r = eval_expr(it, env, e->as.binary.right);
            return eval_binary(it, e->line, e->as.binary.op, l, r);
        }

        case EXPR_CALL: {
            /* Only myon.print is a callable in Steps 0-5. */
            Expr *callee = e->as.call.callee;
            if (callee->kind == EXPR_IDENT && callee->as.ident) {
                /* the compound "myon.print" token becomes ident "myon.print"?
                 * No — it is a dedicated token, handled below via member form. */
            }
            runtime_error(it, e->line, "call of unsupported callee (only myon.print supported so far)");
            return value_nil();
        }

        case EXPR_INDEX:
            runtime_error(it, e->line, "indexing is not supported yet (Step 7)");
            return value_nil();

        case EXPR_MEMBER:
            runtime_error(it, e->line, "member access is not supported yet");
            return value_nil();
    }
    runtime_error(it, e->line, "unknown expression kind");
    return value_nil();
}

/* ------------------------------------------------------------------ */
/* Statement execution                                                 */
/* ------------------------------------------------------------------ */

/* Handle the special case where the parser produced a myon.print call.
 * The lexer tokenises "myon.print" as a single keyword; the parser turns
 * it into an EXPR_CALL whose callee is an EXPR_IDENT? No — myon.print is a
 * primary via the keyword path. To keep Steps 0-5 self-contained we detect
 * print at the expression-statement level. */
static int try_exec_print(Interp *it, Env *env, Expr *e) {
    if (e->kind != EXPR_CALL) return 0;
    Expr *callee = e->as.call.callee;
    if (callee->kind == EXPR_IDENT && strcmp(callee->as.ident, "myon.print") == 0) {
        Value v = builtin_print(it, env, e);
        value_free(&v);
        return 1;
    }
    return 0;
}

static Flow exec_stmt(Interp *it, Env *env, Stmt *s) {
    switch (s->kind) {
        case STMT_SYSTEM:
        case STMT_MODULE:
            /* Declarations: no runtime effect in Steps 0-5. */
            return FLOW_NORMAL;

        case STMT_ASSIGN: {
            Value v = eval_expr(it, env, s->as.assign.value);

            if (s->as.assign.is_compound) {
                Value cur;
                if (!env_get(env, s->as.assign.name, &cur))
                    runtime_error(it, s->line, "compound assignment to undefined variable '%s'",
                                  s->as.assign.name);
                Value res = eval_binary(it, s->line, s->as.assign.compound, cur, v);
                if (!env_set(env, s->as.assign.name, res))
                    runtime_error(it, s->line, "failed to assign '%s'", s->as.assign.name);
                return FLOW_NORMAL;
            }

            /* nil is only valid for the error type (spec 2.4) */
            if (v.type == TYPE_NIL)
                runtime_error(it, s->line,
                    "cannot assign myon.nil to a normal variable (spec 2.4)");

            /* optional type annotation check (spec 14.7 allows omission) */
            if (s->as.assign.has_type &&
                !type_equal(s->as.assign.annotated, v.type)) {
                Type want = s->as.assign.annotated, got = v.type;
                value_free(&v);
                runtime_error(it, s->line,
                    "type annotation mismatch: '%s' declared %s but value is %s",
                    s->as.assign.name, type_name(want), type_name(got));
            }

            if (s->as.assign.extra_count > 0) {
                value_free(&v);
                runtime_error(it, s->line,
                    "multiple-target assignment requires multiple return values (Step 6)");
            }

            /* Define if new, otherwise re-assign. Shadowing rules land in Step 8. */
            if (env_defined_local(env, s->as.assign.name)) {
                env_set(env, s->as.assign.name, v);
            } else {
                env_define(env, s->as.assign.name, v);
            }
            return FLOW_NORMAL;
        }

        case STMT_EXPR: {
            if (try_exec_print(it, env, s->as.expr))
                return FLOW_NORMAL;
            Value v = eval_expr(it, env, s->as.expr);
            value_free(&v);
            return FLOW_NORMAL;
        }

        case STMT_IF: {
            Value c = eval_expr(it, env, s->as.if_stmt.cond);
            if (c.type != TYPE_BOOL) {
                Type t = c.type; value_free(&c);
                runtime_error(it, s->line, "if condition must be bool, got %s", type_name(t));
            }
            int taken = c.as.b;
            if (taken)
                return exec_block(it, env, &s->as.if_stmt.then_body);

            for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
                Value ec = eval_expr(it, env, s->as.if_stmt.elifs[i].cond);
                if (ec.type != TYPE_BOOL) {
                    Type t = ec.type; value_free(&ec);
                    runtime_error(it, s->line, "elif condition must be bool, got %s", type_name(t));
                }
                if (ec.as.b)
                    return exec_block(it, env, &s->as.if_stmt.elifs[i].body);
            }
            if (s->as.if_stmt.has_else)
                return exec_block(it, env, &s->as.if_stmt.else_body);
            return FLOW_NORMAL;
        }

        case STMT_WHILE: {
            for (;;) {
                Value c = eval_expr(it, env, s->as.while_stmt.cond);
                if (c.type != TYPE_BOOL) {
                    Type t = c.type; value_free(&c);
                    runtime_error(it, s->line, "while condition must be bool, got %s", type_name(t));
                }
                if (!c.as.b) break;
                Flow f = exec_block(it, env, &s->as.while_stmt.body);
                if (f == FLOW_BREAK) break;
                /* FLOW_CONTINUE and FLOW_NORMAL both loop again */
            }
            return FLOW_NORMAL;
        }

        case STMT_FOR: {
            if (!s->as.for_stmt.is_range)
                runtime_error(it, s->line,
                    "for-in over arrays is not supported yet (Step 7); use range(...)");

            Value start = eval_expr(it, env, s->as.for_stmt.range_start);
            Value end   = eval_expr(it, env, s->as.for_stmt.range_end);
            if (start.type != TYPE_INT || end.type != TYPE_INT) {
                Type st = start.type, et = end.type;
                value_free(&start); value_free(&end);
                runtime_error(it, s->line, "range(...) requires int bounds, got %s and %s",
                              type_name(st), type_name(et));
            }
            long long lo = start.as.i, hi = end.as.i;

            for (long long i = lo; i < hi; i++) {
                Env *loop = env_new(env);
                env_define(loop, s->as.for_stmt.var, value_int(i));
                Flow f = exec_block(it, loop, &s->as.for_stmt.body);
                env_free(loop);
                if (f == FLOW_BREAK) break;
            }
            return FLOW_NORMAL;
        }

        case STMT_BREAK:    return FLOW_BREAK;
        case STMT_CONTINUE: return FLOW_CONTINUE;

        case STMT_BLOCK: {
            Env *scope = env_new(env);
            Flow f = exec_block(it, scope, &s->as.block);
            env_free(scope);
            return f;
        }

        case STMT_EXPOSE:
            /* Full semantics arrive in Step 8; accept syntactically. */
            return FLOW_NORMAL;
    }
    return FLOW_NORMAL;
}

/* Execute a statement list, propagating break/continue upward. */
static Flow exec_block(Interp *it, Env *env, StmtList *body) {
    for (int i = 0; i < body->count; i++) {
        Flow f = exec_stmt(it, env, body->items[i]);
        if (f != FLOW_NORMAL) return f;
    }
    return FLOW_NORMAL;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int interpret(Program *program) {
    Interp it;
    it.global = env_new(NULL);

    if (setjmp(it.on_error)) {
        env_free(it.global);
        return 1;
    }

    for (int i = 0; i < program->stmts.count; i++) {
        Flow f = exec_stmt(&it, it.global, program->stmts.items[i]);
        if (f == FLOW_BREAK || f == FLOW_CONTINUE) {
            fprintf(stderr, "myon: runtime error: break/continue outside of a loop\n");
            env_free(it.global);
            return 1;
        }
    }

    env_free(it.global);
    return 0;
}
