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
#include "lexer.h"
#include "parser.h"
#include "diag.h"
#include "ffi.h"
#include "ffi_call.h"
#include "ffi_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>

/* Some C standard libraries only expose M_PI / M_E under _GNU_SOURCE or
 * similar feature-test macros; provide portable fallbacks (Phase3.5). */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* ------------------------------------------------------------------ */
/* Interpreter state and control-flow signalling                       */
/* ------------------------------------------------------------------ */

typedef enum { FLOW_NORMAL, FLOW_BREAK, FLOW_CONTINUE, FLOW_RETURN } Flow;

/* registry of declared structs (for constructor / method lookup) */
typedef struct {
    StructDecl **items;
    int          count;
} StructReg;

/* modules that have been loaded (for cycle detection + namespaces) */
typedef struct ModuleEntry {
    char               *path;    /* dotted path */
    char               *alias;   /* alias or NULL */
    int                 loading; /* in-progress flag for cycle detection */
    struct ModuleEntry *next;
} ModuleEntry;

struct Interp {
    Env         *global;
    StructReg    structs;
    ModuleEntry *modules;
    jmp_buf      on_error;
    /* return-value transport */
    Value       *ret_values;
    int          ret_count;
    /* generic type-parameter bindings active in the current call */
    Env         *type_env;   /* name -> not used as value; tracked separately */
    /* retained parsed programs (REPL keeps ASTs alive across inputs) */
    Program    **programs;
    int          program_count;
    /* Phase3 C FFI subsystem (lazily created on first myon.ffi.* use) */
    FFIState    *ffi;
};
typedef struct Interp Interp;

static void runtime_error(Interp *it, int line, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "myon: runtime error at line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    /* P5: show the offending source line.  The interpreter only tracks the
     * line (not the column) for a running expression, so the caret points at
     * the start of the line as a best-effort locator. */
    diag_print_snippet(line, 1);
    longjmp(it->on_error, 1);
}

static Value eval_expr(Interp *it, Env *env, Expr *e);
static Flow  exec_block(Interp *it, Env *env, StmtList *body);
static Flow  exec_stmt(Interp *it, Env *env, Stmt *s);
static Value call_function(Interp *it, int line, Value fn, Value *args, int argc,
                           char **arg_names);

/* ------------------------------------------------------------------ */
/* Struct registry                                                     */
/* ------------------------------------------------------------------ */

static void register_struct(Interp *it, StructDecl *sd) {
    it->structs.items = (StructDecl **)myon_xrealloc(
        it->structs.items, sizeof(StructDecl *) * (it->structs.count + 1));
    it->structs.items[it->structs.count++] = sd;
}

static StructDecl *find_struct(Interp *it, const char *name) {
    for (int i = 0; i < it->structs.count; i++)
        if (strcmp(it->structs.items[i]->name, name) == 0)
            return it->structs.items[i];
    return NULL;
}

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
        case TYPE_BOOL: { long long r = v.as.b; return value_int(r); }
        case TYPE_STR: {
            char *end = NULL;
            long long r = strtoll(v.as.obj->as.str, &end, 10);
            if (end == v.as.obj->as.str || *end != '\0') {
                char *bad = myon_strdup(v.as.obj->as.str);
                value_free(&v);
                runtime_error(it, line, "int(): cannot parse '%s' as integer", bad);
            }
            value_free(&v);
            return value_int(r);
        }
        case TYPE_CHAR: {
            long long r = (unsigned char)v.as.obj->as.str[0];
            value_free(&v);
            return value_int(r);
        }
        default:
            runtime_error(it, line, "int(): unsupported source type %s", value_type_name(&v));
            return value_nil();
    }
}

static Value cast_to_char(Interp *it, int line, Value v) {
    switch (v.type) {
        case TYPE_CHAR: return v;
        case TYPE_INT: {
            char buf[2] = { (char)(v.as.i & 0xFF), '\0' };
            return value_char(myon_strdup(buf));
        }
        case TYPE_STR: {
            if (strlen(v.as.obj->as.str) == 0) {
                value_free(&v);
                runtime_error(it, line, "char(): empty string");
            }
            char buf[2] = { v.as.obj->as.str[0], '\0' };
            value_free(&v);
            return value_char(myon_strdup(buf));
        }
        default:
            runtime_error(it, line, "char(): unsupported source type %s", value_type_name(&v));
            return value_nil();
    }
}

/* ------------------------------------------------------------------ */
/* Arithmetic / comparison with strict typing (spec 2.2)               */
/* ------------------------------------------------------------------ */

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
    if (op == OP_EQ || op == OP_NEQ) {
        if (l.type == TYPE_NIL || r.type == TYPE_NIL) {
            int both_nil = (l.type == TYPE_NIL && r.type == TYPE_NIL);
            int one_err_nil =
                (l.type == TYPE_ERROR && r.type == TYPE_NIL) ||
                (r.type == TYPE_ERROR && l.type == TYPE_NIL);
            if (!both_nil && !one_err_nil) {
                Type lt = l.type, rt = r.type;
                value_free(&l); value_free(&r);
                runtime_error(it, line,
                    "myon.nil may only be compared with error values (spec 2.4); got %s and %s",
                    type_name(lt), type_name(rt));
            }
            int equal = both_nil;
            value_free(&l); value_free(&r);
            return value_bool(op == OP_EQ ? equal : !equal);
        }
    }

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
                default:
                    return value_bool(compare_ordered((double)a, (double)b, op));
            }
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
                default: return value_bool(compare_ordered(a, b, op));
            }
        }
        case TYPE_STR: {
            const char *ls = l.as.obj->as.str, *rs = r.as.obj->as.str;
            if (op == OP_ADD) {
                size_t la = strlen(ls), lb = strlen(rs);
                char *buf = (char *)myon_xmalloc(la + lb + 1);
                memcpy(buf, ls, la);
                memcpy(buf + la, rs, lb);
                buf[la + lb] = '\0';
                value_free(&l); value_free(&r);
                return value_str(buf);
            }
            if (op == OP_EQ || op == OP_NEQ) {
                int eq = strcmp(ls, rs) == 0;
                value_free(&l); value_free(&r);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            value_free(&l); value_free(&r);
            runtime_error(it, line, "unsupported operator on str");
            break;
        }
        case TYPE_CHAR: {
            int eq = strcmp(l.as.obj->as.str, r.as.obj->as.str) == 0;
            if (op == OP_EQ || op == OP_NEQ) {
                value_free(&l); value_free(&r);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            value_free(&l); value_free(&r);
            runtime_error(it, line, "unsupported operator on char");
            break;
        }
        case TYPE_BOOL: {
            int eq = (l.as.b == r.as.b);
            if (op == OP_EQ || op == OP_NEQ)
                return value_bool(op == OP_EQ ? eq : !eq);
            runtime_error(it, line, "unsupported operator on bool");
            break;
        }
        case TYPE_ERROR: {
            int eq = strcmp(l.as.obj->as.str, r.as.obj->as.str) == 0;
            if (op == OP_EQ || op == OP_NEQ) {
                value_free(&l); value_free(&r);
                return value_bool(op == OP_EQ ? eq : !eq);
            }
            value_free(&l); value_free(&r);
            runtime_error(it, line, "unsupported operator on error");
            break;
        }
        default:
            runtime_error(it, line, "unsupported operand type %s", type_name(l.type));
    }
    return value_nil();
}

/* ------------------------------------------------------------------ */
/* String interpolation (spec section 4, Step 10)                      */
/* ------------------------------------------------------------------ */

/* Evaluate a raw string body, expanding `{expr}` fragments.
 * `{{` and `}}` are literal braces. */
static Value interpolate_string(Interp *it, Env *env, int line, const char *raw) {
    size_t cap = strlen(raw) + 1, len = 0;
    char *out = (char *)myon_xmalloc(cap);
    out[0] = '\0';

    const char *s = raw;
    while (*s) {
        if (s[0] == '{' && s[1] == '{') { /* literal { */
            if (len + 1 >= cap) { cap *= 2; out = myon_xrealloc(out, cap); }
            out[len++] = '{'; out[len] = '\0'; s += 2; continue;
        }
        if (s[0] == '}' && s[1] == '}') {
            if (len + 1 >= cap) { cap *= 2; out = myon_xrealloc(out, cap); }
            out[len++] = '}'; out[len] = '\0'; s += 2; continue;
        }
        if (s[0] == '{') {
            /* find matching '}' */
            const char *end = strchr(s + 1, '}');
            if (!end) {
                free(out);
                runtime_error(it, line, "unterminated '{' in string interpolation");
            }
            size_t exprlen = (size_t)(end - (s + 1));
            char *exprsrc = myon_strndup(s + 1, exprlen);

            /* lex + parse the sub-expression */
            TokenList tl;
            if (!lexer_tokenize(exprsrc, &tl)) {
                free(exprsrc); free(out);
                runtime_error(it, line, "lexical error in interpolation '{%s}'", "");
            }
            Program *sub = parser_parse(&tl);
            free(exprsrc);
            if (!sub || sub->stmts.count != 1 || sub->stmts.items[0]->kind != STMT_EXPR) {
                if (sub) program_free(sub);
                token_list_free(&tl);
                free(out);
                runtime_error(it, line, "invalid expression in string interpolation");
            }
            Value v = eval_expr(it, env, sub->stmts.items[0]->as.expr);
            char *piece = value_to_cstr(&v);
            value_free(&v);
            program_free(sub);
            token_list_free(&tl);

            size_t pl = strlen(piece);
            while (len + pl + 1 >= cap) { cap *= 2; out = myon_xrealloc(out, cap); }
            memcpy(out + len, piece, pl + 1);
            len += pl;
            free(piece);
            s = end + 1;
            continue;
        }
        if (len + 1 >= cap) { cap *= 2; out = myon_xrealloc(out, cap); }
        out[len++] = *s++;
        out[len] = '\0';
    }
    return value_str(out);
}

/* ------------------------------------------------------------------ */
/* Builtins: myon.print / myon.input                                   */
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

static Value builtin_input(Interp *it, Env *env, Expr *call) {
    if (call->as.call.arg_count > 0) {
        Value prompt = eval_expr(it, env, call->as.call.args[0]);
        char *s = value_to_cstr(&prompt);
        fputs(s, stdout);
        fflush(stdout);
        free(s);
        value_free(&prompt);
    }
    size_t cap = 128, len = 0;
    char *buf = (char *)myon_xmalloc(cap);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (len + 1 >= cap) { cap *= 2; buf = myon_xrealloc(buf, cap); }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return value_str(buf);
}

/* ------------------------------------------------------------------ */
/* Standard library: myon.math / myon.string (Step 16)                 */
/* ------------------------------------------------------------------ */

static Value eval_arg(Interp *it, Env *env, Expr *call, int i) {
    return eval_expr(it, env, call->as.call.args[i]);
}

static double as_number(Interp *it, int line, Value v) {
    if (v.type == TYPE_INT) { double d = (double)v.as.i; return d; }
    if (v.type == TYPE_FLOAT) return v.as.f;
    runtime_error(it, line, "expected a numeric argument, got %s", type_name(v.type));
    return 0;
}

/* Numeric type-preservation helper (Phase3.5, Step1).
 * Returns 1 when *both* arguments are TYPE_INT (so the caller can compute the
 * result in int precision without a lossy round-trip through double), and 0
 * when at least one operand is a float.  Non-numeric arguments raise a
 * runtime_error, exactly like as_number(). */
static int both_int(Interp *it, int line, Value a, Value b) {
    (void)as_number(it, line, a); /* reuse as_number purely for its type check */
    (void)as_number(it, line, b);
    return a.type == TYPE_INT && b.type == TYPE_INT;
}

/* UTF-8 helpers (Phase3.5, Step2).
 * Count the number of Unicode code points in a NUL-terminated UTF-8 byte
 * string.  Robust against malformed sequences: an invalid lead/continuation
 * byte is counted as a single character rather than crashing or looping
 * forever (Myon str values may carry arbitrary bytes via FFI). */
static long long utf8_char_count(const char *s) {
    long long count = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = *p;
        int extra;
        if      ((c & 0x80) == 0x00) extra = 0; /* 0xxxxxxx */
        else if ((c & 0xE0) == 0xC0) extra = 1; /* 110xxxxx */
        else if ((c & 0xF0) == 0xE0) extra = 2; /* 1110xxxx */
        else if ((c & 0xF8) == 0xF0) extra = 3; /* 11110xxx */
        else { p++; count++; continue; }        /* invalid lead byte */
        p++;
        int ok = 1;
        for (int k = 0; k < extra; k++) {
            if ((*p & 0xC0) != 0x80) { ok = 0; break; }
            p++;
        }
        (void)ok; /* robustness first: count one code point regardless */
        count++;
    }
    return count;
}

/* Return the byte offset at which code point `char_idx` (0-indexed) begins in
 * the NUL-terminated UTF-8 string `s`.  When `char_idx` reaches or exceeds the
 * number of code points, the byte length (offset of the NUL terminator) is
 * returned, so callers can address the one-past-the-end position for slicing.
 * Malformed sequences use the same robust fallback as utf8_char_count(). */
static long long utf8_byte_offset(const char *s, long long char_idx) {
    const unsigned char *base = (const unsigned char *)s;
    const unsigned char *p = base;
    long long seen = 0;
    while (*p && seen < char_idx) {
        unsigned char c = *p;
        int extra;
        if      ((c & 0x80) == 0x00) extra = 0; /* 0xxxxxxx */
        else if ((c & 0xE0) == 0xC0) extra = 1; /* 110xxxxx */
        else if ((c & 0xF0) == 0xE0) extra = 2; /* 1110xxxx */
        else if ((c & 0xF8) == 0xF0) extra = 3; /* 11110xxx */
        else { p++; seen++; continue; }         /* invalid lead byte */
        p++;
        for (int k = 0; k < extra && *p; k++) {
            if ((*p & 0xC0) != 0x80) break;      /* invalid continuation */
            p++;
        }
        seen++;
    }
    return (long long)(p - base);
}

/* Build a 2-value tuple (value, error) for the Rust/Go-style return
 * convention (spec 6.2).  Uses the same untyped-array tuple representation as
 * multi-value `ret`, so multiple-target assignment can unpack it. */
static Value make_result_pair(Value ok, Value err) {
    Value tup = value_array(NULL);
    array_push(&tup, ok);
    array_push(&tup, err);
    return tup;
}

/* ------------------------------------------------------------------ */
/* Standard library: myon.file.* (Step P4 — file I/O)                  */
/*                                                                     */
/* Failures do not raise a Myon runtime_error; instead they are        */
/* surfaced as an `error(...)` value in the second tuple slot so the   */
/* caller can inspect it with `myon.if err != myon.nil` (spec 6.2).    */
/* ------------------------------------------------------------------ */

/* Dispatch a "myon.file.<fn>" call. Returns 1 and sets *out if handled. */
static int call_file_io(Interp *it, Env *env, const char *name, Expr *call, Value *out) {
    int line = call->line;

    /* myon.file.read(path) ret str, error */
    if (strcmp(name, "myon.file.read") == 0) {
        Value p = eval_arg(it, env, call, 0);
        if (p.type != TYPE_STR) { value_free(&p); runtime_error(it, line, "myon.file.read expects a str path"); }
        const char *path = p.as.obj->as.str;
        if (!path) { value_free(&p); *out = make_result_pair(value_str(myon_strdup("")), value_error(myon_strdup("null path"))); return 1; }
        FILE *f = fopen(path, "rb");
        if (!f) {
            char msg[512];
            snprintf(msg, sizeof(msg), "cannot open '%s' for reading", path);
            value_free(&p);
            *out = make_result_pair(value_str(myon_strdup("")), value_error(myon_strdup(msg)));
            return 1;
        }
        size_t cap = 4096, len = 0;
        char *buf = (char *)myon_xmalloc(cap);
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (len + 1 >= cap) { cap *= 2; buf = (char *)myon_xrealloc(buf, cap); }
            buf[len++] = (char)c;
        }
        buf[len] = '\0';
        fclose(f);
        value_free(&p);
        *out = make_result_pair(value_str(buf), value_nil());
        return 1;
    }

    /* myon.file.write(path, content) / myon.file.append(path, content)
     *   ret bool, error */
    if (strcmp(name, "myon.file.write") == 0 || strcmp(name, "myon.file.append") == 0) {
        int append = (strcmp(name, "myon.file.append") == 0);
        Value p = eval_arg(it, env, call, 0);
        Value cnt = eval_arg(it, env, call, 1);
        if (p.type != TYPE_STR || cnt.type != TYPE_STR) {
            value_free(&p); value_free(&cnt);
            runtime_error(it, line, "%s expects (str path, str content)", name);
        }
        const char *path = p.as.obj->as.str;
        const char *content = cnt.as.obj->as.str;
        if (!path) {
            value_free(&p); value_free(&cnt);
            *out = make_result_pair(value_bool(0), value_error(myon_strdup("null path")));
            return 1;
        }
        FILE *f = fopen(path, append ? "ab" : "wb");
        if (!f) {
            char msg[512];
            snprintf(msg, sizeof(msg), "cannot open '%s' for %s", path, append ? "appending" : "writing");
            value_free(&p); value_free(&cnt);
            *out = make_result_pair(value_bool(0), value_error(myon_strdup(msg)));
            return 1;
        }
        size_t n = strlen(content);
        size_t w = fwrite(content, 1, n, f);
        int close_err = fclose(f);
        if (w != n || close_err != 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "write to '%s' failed", path);
            value_free(&p); value_free(&cnt);
            *out = make_result_pair(value_bool(0), value_error(myon_strdup(msg)));
            return 1;
        }
        value_free(&p); value_free(&cnt);
        *out = make_result_pair(value_bool(1), value_nil());
        return 1;
    }

    /* myon.file.exists(path) ret bool */
    if (strcmp(name, "myon.file.exists") == 0) {
        Value p = eval_arg(it, env, call, 0);
        if (p.type != TYPE_STR) { value_free(&p); runtime_error(it, line, "myon.file.exists expects a str path"); }
        const char *path = p.as.obj->as.str;
        int exists = 0;
        if (path) {
            FILE *f = fopen(path, "rb");
            if (f) { exists = 1; fclose(f); }
        }
        value_free(&p);
        *out = value_bool(exists);
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Standard library: myon.ffi.* (Phase3 — C FFI)                       */
/*                                                                     */
/* Like myon.file.*, failures surface as an `error(...)` value in the  */
/* second tuple slot rather than aborting with a runtime_error, so the */
/* caller can inspect them via `myon.if err != myon.nil` (spec 6.2).   */
/* ------------------------------------------------------------------ */

/* Lazily create (once) the interpreter's FFI subsystem. */
static FFIState *ffi_get_state(Interp *it) {
    if (!it->ffi) it->ffi = ffi_state_create();
    return it->ffi;
}

/* Convert one already-evaluated Myon Value into an FFIArgValue according to the
 * signature character `sig`.  Returns 1 on success, 0 on a type mismatch (and
 * writes a heap error message into *err).  `st` is used to resolve 'b'
 * (memory-block) arguments to their real address. */
static int ffi_convert_arg(FFIState *st, char sig, const Value *v,
                           FFIArgValue *av, FFIArgKind *ak, char **err) {
    switch (sig) {
        case 'i':
            if (v->type == TYPE_INT)  { av->i = v->as.i; }
            else if (v->type == TYPE_BOOL) { av->i = v->as.b; }
            else { *err = myon_strdup("ffi: 'i' argument expects int/bool"); return 0; }
            *ak = FFI_ARG_I64;
            return 1;
        case 'p':
            /* pointer handles are carried as Myon int */
            if (v->type == TYPE_INT) { av->p = (void *)(size_t)v->as.i; }
            else { *err = myon_strdup("ffi: 'p' argument expects int (pointer handle)"); return 0; }
            *ak = FFI_ARG_PTR;
            return 1;
        case 'b': {
            /* Phase3.1: 'b' takes a Myon.ffi.alloc block ID and passes the
             * block's real memory address to the C function (as a pointer).
             * Guards against invalid IDs so a bogus value can never crash. */
            if (v->type != TYPE_INT) { *err = myon_strdup("ffi: 'b' argument expects int (block id)"); return 0; }
            void *p = ffi_mem_ptr(st, v->as.i);
            if (!p) { *err = myon_strdup("ffi: 'b' argument is not a valid memory block id"); return 0; }
            av->p = p;
            *ak = FFI_ARG_PTR;
            return 1;
        }
        case 'd':
            if (v->type == TYPE_FLOAT) { av->d = v->as.f; }
            else if (v->type == TYPE_INT) { av->d = (double)v->as.i; }
            else { *err = myon_strdup("ffi: 'd' argument expects float/int"); return 0; }
            *ak = FFI_ARG_F64;
            return 1;
        case 's':
            if (v->type == TYPE_STR) { av->s = v->as.obj->as.str; }
            else { *err = myon_strdup("ffi: 's' argument expects str"); return 0; }
            *ak = FFI_ARG_STR;
            return 1;
        default: {
            char buf[64];
            snprintf(buf, sizeof(buf), "ffi: unknown signature char '%c'", sig);
            *err = myon_strdup(buf);
            return 0;
        }
    }
}

/* Shared implementation for myon.ffi.call_{i,d,p,v}. */
static Value ffi_do_call(Interp *it, Env *env, Expr *call, FFIRetKind ret_kind) {
    int argc = call->as.call.arg_count;

    Value fail_default = (ret_kind == FFI_RET_F64) ? value_float(0.0)
                       : (ret_kind == FFI_RET_VOID) ? value_bool(0)
                       : value_int(0);

    if (argc < 3) {
        return make_result_pair(fail_default,
            value_error(myon_strdup("ffi.call_* expects (handle, name, sig, ...)")));
    }

    Value hv   = eval_arg(it, env, call, 0);
    Value name = eval_arg(it, env, call, 1);
    Value sigv = eval_arg(it, env, call, 2);

    if (hv.type != TYPE_INT || name.type != TYPE_STR || sigv.type != TYPE_STR) {
        value_free(&hv); value_free(&name); value_free(&sigv);
        return make_result_pair(fail_default,
            value_error(myon_strdup("ffi.call_* expects (int handle, str name, str sig)")));
    }

    long long handle = hv.as.i;
    const char *sym  = name.as.obj->as.str;
    const char *sig  = sigv.as.obj->as.str;
    int nsig = (int)strlen(sig);

    /* number of provided variadic args must match sig length */
    int provided = argc - 3;
    if (nsig != provided) {
        value_free(&hv); value_free(&name); value_free(&sigv);
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "ffi.call_*: signature has %d args but %d were provided",
                 nsig, provided);
        return make_result_pair(fail_default, value_error(myon_strdup(buf)));
    }
    if (nsig > 6) {
        value_free(&hv); value_free(&name); value_free(&sigv);
        return make_result_pair(fail_default,
            value_error(myon_strdup("ffi.call_*: at most 6 arguments are supported")));
    }

    /* resolve the symbol */
    void *fn = ffi_lookup_symbol(ffi_get_state(it), handle, sym);
    if (!fn) {
        char buf[256];
        snprintf(buf, sizeof(buf), "ffi.call_*: symbol '%s' not found in handle %lld",
                 sym, handle);
        value_free(&hv); value_free(&name); value_free(&sigv);
        return make_result_pair(fail_default, value_error(myon_strdup(buf)));
    }

    /* evaluate and convert the variadic arguments in signature order */
    FFIArgValue args[6];
    FFIArgKind  kinds[6];
    Value       argvals[6];
    int         nargvals = 0;
    char       *cerr = NULL;
    int         ok = 1;

    for (int i = 0; i < nsig; i++) {
        Value av = eval_arg(it, env, call, 3 + i);
        argvals[nargvals++] = av;
        if (!ffi_convert_arg(ffi_get_state(it), sig[i], &av, &args[i], &kinds[i], &cerr)) {
            ok = 0;
            break;
        }
    }

    Value result;
    if (!ok) {
        result = make_result_pair(fail_default, value_error(cerr));
    } else {
        FFIRetValue rv;
        if (!ffi_call_dispatch(fn, args, kinds, nsig, ret_kind, &rv)) {
            value_free(&fail_default);
            result = make_result_pair(
                (ret_kind == FFI_RET_F64) ? value_float(0.0)
                : (ret_kind == FFI_RET_VOID) ? value_bool(0)
                : value_int(0),
                value_error(myon_strdup(
                    "ffi.call_*: unsupported argument arity/ordering "
                    "(integer-class args must precede double args; max 6)")));
        } else {
            value_free(&fail_default);
            Value ok_val;
            switch (ret_kind) {
                case FFI_RET_F64:  ok_val = value_float(rv.d); break;
                case FFI_RET_PTR:  ok_val = value_int((long long)(size_t)rv.p); break;
                case FFI_RET_VOID: ok_val = value_bool(1); break;
                case FFI_RET_I64:
                default:           ok_val = value_int(rv.i); break;
            }
            result = make_result_pair(ok_val, value_nil());
        }
    }

    for (int i = 0; i < nargvals; i++) value_free(&argvals[i]);
    value_free(&hv); value_free(&name); value_free(&sigv);
    return result;
}

/* Dispatch a "myon.ffi.<fn>" call. Returns 1 and sets *out if handled. */
static int call_ffi(Interp *it, Env *env, const char *name, Expr *call, Value *out) {
    int line = call->line;

    /* myon.ffi.load(path) ret int, error */
    if (strcmp(name, "myon.ffi.load") == 0) {
        Value p = eval_arg(it, env, call, 0);
        if (p.type != TYPE_STR) { value_free(&p); runtime_error(it, line, "myon.ffi.load expects a str path"); }
        if (!ffi_platform_supported()) {
            value_free(&p);
            *out = make_result_pair(value_int(-1),
                value_error(myon_strdup("FFI is not supported on this platform yet (Phase3 stub)")));
            return 1;
        }
        const char *path = p.as.obj->as.str;
        char *err = NULL;
        long long h = ffi_load(ffi_get_state(it), path, &err);
        if (h < 0) {
            char buf[512];
            snprintf(buf, sizeof(buf), "ffi.load: %s", err ? err : "load failed");
            free(err);
            value_free(&p);
            *out = make_result_pair(value_int(-1), value_error(myon_strdup(buf)));
            return 1;
        }
        value_free(&p);
        *out = make_result_pair(value_int(h), value_nil());
        return 1;
    }

    /* myon.ffi.close(handle) ret bool, error */
    if (strcmp(name, "myon.ffi.close") == 0) {
        Value hv = eval_arg(it, env, call, 0);
        if (hv.type != TYPE_INT) { value_free(&hv); runtime_error(it, line, "myon.ffi.close expects an int handle"); }
        int ok = ffi_close(ffi_get_state(it), hv.as.i);
        value_free(&hv);
        if (!ok) {
            *out = make_result_pair(value_bool(0),
                value_error(myon_strdup("ffi.close: invalid or already-closed handle")));
        } else {
            *out = make_result_pair(value_bool(1), value_nil());
        }
        return 1;
    }

    if (strcmp(name, "myon.ffi.call_i") == 0) { *out = ffi_do_call(it, env, call, FFI_RET_I64);  return 1; }
    if (strcmp(name, "myon.ffi.call_d") == 0) { *out = ffi_do_call(it, env, call, FFI_RET_F64);  return 1; }
    if (strcmp(name, "myon.ffi.call_p") == 0) { *out = ffi_do_call(it, env, call, FFI_RET_PTR);  return 1; }
    if (strcmp(name, "myon.ffi.call_v") == 0) { *out = ffi_do_call(it, env, call, FFI_RET_VOID); return 1; }

    /* myon.ffi.alloc(size) ret int, error  (Phase3.1) */
    if (strcmp(name, "myon.ffi.alloc") == 0) {
        Value sv = eval_arg(it, env, call, 0);
        if (sv.type != TYPE_INT) { value_free(&sv); runtime_error(it, line, "myon.ffi.alloc expects an int size"); }
        long long size = sv.as.i;
        value_free(&sv);
        if (size <= 0) {
            *out = make_result_pair(value_int(-1),
                value_error(myon_strdup("ffi.alloc: size must be positive")));
            return 1;
        }
        long long id = ffi_mem_alloc(ffi_get_state(it), size);
        if (id < 0) {
            *out = make_result_pair(value_int(-1),
                value_error(myon_strdup("ffi.alloc: out of memory")));
        } else {
            *out = make_result_pair(value_int(id), value_nil());
        }
        return 1;
    }

    /* myon.ffi.free(block_id) ret bool, error  (Phase3.1) */
    if (strcmp(name, "myon.ffi.free") == 0) {
        Value bv = eval_arg(it, env, call, 0);
        if (bv.type != TYPE_INT) { value_free(&bv); runtime_error(it, line, "myon.ffi.free expects an int block id"); }
        int ok = ffi_mem_free(ffi_get_state(it), bv.as.i);
        value_free(&bv);
        if (!ok) {
            *out = make_result_pair(value_bool(0),
                value_error(myon_strdup("ffi.free: invalid or already-freed block id")));
        } else {
            *out = make_result_pair(value_bool(1), value_nil());
        }
        return 1;
    }

    /* myon.ffi.read_cstr(addr, max_len) ret str, error  (Phase3.1) */
    if (strcmp(name, "myon.ffi.read_cstr") == 0) {
        Value av = eval_arg(it, env, call, 0);
        Value mv = eval_arg(it, env, call, 1);
        if (av.type != TYPE_INT || mv.type != TYPE_INT) {
            value_free(&av); value_free(&mv);
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("ffi.read_cstr expects (int addr, int max_len)")));
            return 1;
        }
        long long addr = av.as.i;
        long long max_len = mv.as.i;
        value_free(&av); value_free(&mv);

        /* upper bound on max_len to avoid runaway reads (16 MB) */
        const long long FFI_READ_CSTR_LIMIT = 16LL * 1024 * 1024;
        if (max_len <= 0 || max_len > FFI_READ_CSTR_LIMIT) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("ffi.read_cstr: max_len must be in 1..16777216")));
            return 1;
        }
        if (addr == 0) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("ffi.read_cstr: NULL address")));
            return 1;
        }
        char *s = ffi_read_cstring(addr, max_len);
        if (!s) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("ffi.read_cstr: could not read string")));
        } else {
            *out = make_result_pair(value_str(s), value_nil());
        }
        return 1;
    }

    /* myon.ffi.write_bytes(block_id, offset, data) ret bool, error  (Phase3.1)
     *
     * Writes the bytes of the Myon str `data` into a previously allocated
     * memory block.  Myon's str is a NUL-terminated char* (no length field),
     * so the write covers strlen(data) bytes — embedded NUL bytes truncate.
     * This limitation is documented in docs/myon_spec.md (10.3.1). */
    if (strcmp(name, "myon.ffi.write_bytes") == 0) {
        Value bv = eval_arg(it, env, call, 0);
        Value ov = eval_arg(it, env, call, 1);
        Value dv = eval_arg(it, env, call, 2);
        if (bv.type != TYPE_INT || ov.type != TYPE_INT || dv.type != TYPE_STR) {
            value_free(&bv); value_free(&ov); value_free(&dv);
            *out = make_result_pair(value_bool(0),
                value_error(myon_strdup(
                    "ffi.write_bytes expects (int block_id, int offset, str data)")));
            return 1;
        }
        long long block_id = bv.as.i;
        long long offset   = ov.as.i;
        const char *data   = dv.as.obj->as.str;
        long long len      = (long long)strlen(data);
        value_free(&bv); value_free(&ov);
        int ok = ffi_mem_write(ffi_get_state(it), block_id, offset,
                               (const unsigned char *)data, len);
        value_free(&dv);
        if (!ok) {
            *out = make_result_pair(value_bool(0),
                value_error(myon_strdup(
                    "ffi.write_bytes: invalid block id or out-of-range write")));
        } else {
            *out = make_result_pair(value_bool(1), value_nil());
        }
        return 1;
    }

    /* myon.ffi.read_bytes(block_id, offset, len) ret str, error  (Phase3.1)
     *
     * Reads `len` bytes from a block into a Myon str.  Because str is
     * NUL-terminated, the result is best-effort for binary data (an embedded
     * NUL byte truncates the visible string). */
    if (strcmp(name, "myon.ffi.read_bytes") == 0) {
        Value bv = eval_arg(it, env, call, 0);
        Value ov = eval_arg(it, env, call, 1);
        Value lv = eval_arg(it, env, call, 2);
        if (bv.type != TYPE_INT || ov.type != TYPE_INT || lv.type != TYPE_INT) {
            value_free(&bv); value_free(&ov); value_free(&lv);
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup(
                    "ffi.read_bytes expects (int block_id, int offset, int len)")));
            return 1;
        }
        long long block_id = bv.as.i;
        long long offset   = ov.as.i;
        long long len      = lv.as.i;
        value_free(&bv); value_free(&ov); value_free(&lv);
        if (len < 0) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("ffi.read_bytes: len must be non-negative")));
            return 1;
        }
        unsigned char *buf = ffi_mem_read(ffi_get_state(it), block_id, offset, len);
        if (!buf) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup(
                    "ffi.read_bytes: invalid block id or out-of-range read")));
        } else {
            /* buf is NUL-terminated by ffi_mem_read; hand it to value_str. */
            *out = make_result_pair(value_str((char *)buf), value_nil());
        }
        return 1;
    }

    /* myon.ffi.read_i64(block_id, offset) ret int, error  (Phase3.1)
     *
     * Reads 8 bytes as a little-endian int64.  Convenience for pulling an
     * out-parameter pointer (e.g. sqlite3* from sqlite3_open) out of a block
     * as a plain address value. */
    if (strcmp(name, "myon.ffi.read_i64") == 0) {
        Value bv = eval_arg(it, env, call, 0);
        Value ov = eval_arg(it, env, call, 1);
        if (bv.type != TYPE_INT || ov.type != TYPE_INT) {
            value_free(&bv); value_free(&ov);
            *out = make_result_pair(value_int(0),
                value_error(myon_strdup(
                    "ffi.read_i64 expects (int block_id, int offset)")));
            return 1;
        }
        long long block_id = bv.as.i;
        long long offset   = ov.as.i;
        value_free(&bv); value_free(&ov);
        long long v = 0;
        int ok = ffi_mem_read_i64(ffi_get_state(it), block_id, offset, &v);
        if (!ok) {
            *out = make_result_pair(value_int(0),
                value_error(myon_strdup(
                    "ffi.read_i64: invalid block id or out-of-range read")));
        } else {
            *out = make_result_pair(value_int(v), value_nil());
        }
        return 1;
    }

    return 0;
}

/* dispatch a "myon.math.<fn>" or "myon.string.<fn>" call.
 * Returns 1 and sets *out if handled. */
static int call_stdlib(Interp *it, Env *env, const char *name, Expr *call, Value *out) {
    int argc = call->as.call.arg_count;
    int line = call->line;

    /* ---- myon.file (file I/O, P4) ---- */
    if (strncmp(name, "myon.file.", 10) == 0) {
        if (call_file_io(it, env, name, call, out)) return 1;
    }

    /* ---- myon.ffi (C FFI, Phase3) ---- */
    if (strncmp(name, "myon.ffi.", 9) == 0) {
        if (call_ffi(it, env, name, call, out)) return 1;
    }

    /* ---- myon.math ---- */
    if (strcmp(name, "myon.math.sqrt") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(sqrt(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.pow") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        *out = value_float(pow(as_number(it, line, a), as_number(it, line, b)));
        value_free(&a); value_free(&b); return 1;
    }
    if (strcmp(name, "myon.math.abs") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type == TYPE_INT) { long long v = a.as.i < 0 ? -a.as.i : a.as.i; *out = value_int(v); }
        else *out = value_float(fabs(as_number(it, line, a)));
        value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.floor") == 0) {
        Value a = eval_arg(it, env, call, 0);
        /* floor(int) == int: skip the lossy double round-trip (Step1). */
        if (a.type == TYPE_INT) { *out = value_int(a.as.i); value_free(&a); return 1; }
        *out = value_int((long long)floor(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.ceil") == 0) {
        Value a = eval_arg(it, env, call, 0);
        /* ceil(int) == int: skip the lossy double round-trip (Step1). */
        if (a.type == TYPE_INT) { *out = value_int(a.as.i); value_free(&a); return 1; }
        *out = value_int((long long)ceil(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.max") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        if (both_int(it, line, a, b)) {
            /* Compare in int64 precision — no double round-trip, so values
             * above 2^53 are handled exactly (Step1). */
            *out = value_int(a.as.i > b.as.i ? a.as.i : b.as.i);
        } else {
            double x = as_number(it, line, a), y = as_number(it, line, b);
            *out = value_float(x > y ? x : y);
        }
        value_free(&a); value_free(&b); return 1;
    }
    if (strcmp(name, "myon.math.min") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        if (both_int(it, line, a, b)) {
            *out = value_int(a.as.i < b.as.i ? a.as.i : b.as.i);
        } else {
            double x = as_number(it, line, a), y = as_number(it, line, b);
            *out = value_float(x < y ? x : y);
        }
        value_free(&a); value_free(&b); return 1;
    }

    /* ---- myon.math (Phase3.5 extension, Step3) ---- */

    /* Trigonometric / inverse-trig (radians, always float). */
    if (strcmp(name, "myon.math.sin") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(sin(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.cos") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(cos(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.tan") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(tan(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.asin") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(asin(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.acos") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(acos(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.atan") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(atan(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.atan2") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        *out = value_float(atan2(as_number(it, line, a), as_number(it, line, b)));
        value_free(&a); value_free(&b); return 1;
    }

    /* Logarithms / exponential (always float). */
    if (strcmp(name, "myon.math.log") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(log(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.log2") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(log2(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.log10") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(log10(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.exp") == 0) {
        Value a = eval_arg(it, env, call, 0);
        *out = value_float(exp(as_number(it, line, a))); value_free(&a); return 1;
    }

    /* round / trunc: always return int; int input passes through unchanged. */
    if (strcmp(name, "myon.math.round") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type == TYPE_INT) { *out = value_int(a.as.i); value_free(&a); return 1; }
        *out = value_int((long long)round(as_number(it, line, a))); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.math.trunc") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type == TYPE_INT) { *out = value_int(a.as.i); value_free(&a); return 1; }
        *out = value_int((long long)trunc(as_number(it, line, a))); value_free(&a); return 1;
    }

    /* mod: int% for two ints, fmod otherwise; division by zero returns a
     * (value, error) pair rather than raising a runtime_error. */
    if (strcmp(name, "myon.math.mod") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        if (both_int(it, line, a, b)) {
            if (b.as.i == 0) {
                *out = make_result_pair(value_nil(), value_error(myon_strdup("division by zero")));
            } else {
                *out = make_result_pair(value_int(a.as.i % b.as.i), value_nil());
            }
        } else {
            double x = as_number(it, line, a), y = as_number(it, line, b);
            if (y == 0.0) {
                *out = make_result_pair(value_nil(), value_error(myon_strdup("division by zero")));
            } else {
                *out = make_result_pair(value_float(fmod(x, y)), value_nil());
            }
        }
        value_free(&a); value_free(&b); return 1;
    }

    /* sign: +1 / -1 / 0, always int; int input uses int comparison only. */
    if (strcmp(name, "myon.math.sign") == 0) {
        Value a = eval_arg(it, env, call, 0);
        long long s;
        if (a.type == TYPE_INT) {
            s = (a.as.i > 0) - (a.as.i < 0);
        } else {
            double x = as_number(it, line, a);
            s = (x > 0.0) - (x < 0.0);
        }
        *out = value_int(s); value_free(&a); return 1;
    }

    /* clamp(x, lo, hi): int math when all three are int, else float.
     * hi < lo is defined to always return hi. */
    if (strcmp(name, "myon.math.clamp") == 0) {
        Value x = eval_arg(it, env, call, 0);
        Value lo = eval_arg(it, env, call, 1);
        Value hi = eval_arg(it, env, call, 2);
        int all_int = (x.type == TYPE_INT && lo.type == TYPE_INT && hi.type == TYPE_INT);
        /* Type-check every operand even in the float path. */
        (void)as_number(it, line, x);
        (void)as_number(it, line, lo);
        (void)as_number(it, line, hi);
        if (all_int) {
            long long xv = x.as.i, lv = lo.as.i, hv = hi.as.i;
            long long r = xv < lv ? lv : (xv > hv ? hv : xv);
            if (hv < lv) r = hv;
            *out = value_int(r);
        } else {
            double xv = as_number(it, line, x);
            double lv = as_number(it, line, lo);
            double hv = as_number(it, line, hi);
            double r = xv < lv ? lv : (xv > hv ? hv : xv);
            if (hv < lv) r = hv;
            *out = value_float(r);
        }
        value_free(&x); value_free(&lo); value_free(&hi); return 1;
    }

    /* Constants exposed as 0-argument functions. */
    if (strcmp(name, "myon.math.pi") == 0) {
        *out = value_float(M_PI); return 1;
    }
    if (strcmp(name, "myon.math.e") == 0) {
        *out = value_float(M_E); return 1;
    }

    /* ---- myon.string ---- */
    if (strcmp(name, "myon.string.length") == 0) {
        /* Unicode code-point count, not byte count (Step2). */
        Value a = eval_arg(it, env, call, 0);
        if (a.type != TYPE_STR) { value_free(&a); runtime_error(it, line, "myon.string.length expects str"); }
        *out = value_int(utf8_char_count(a.as.obj->as.str));
        value_free(&a); return 1;
    }
    if (strcmp(name, "myon.string.byte_length") == 0) {
        /* Raw byte length (strlen), for callers that need the encoded size. */
        Value a = eval_arg(it, env, call, 0);
        if (a.type != TYPE_STR) { value_free(&a); runtime_error(it, line, "myon.string.byte_length expects str"); }
        *out = value_int((long long)strlen(a.as.obj->as.str));
        value_free(&a); return 1;
    }
    if (strcmp(name, "myon.string.concat") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        if (a.type != TYPE_STR || b.type != TYPE_STR) {
            value_free(&a); value_free(&b);
            runtime_error(it, line, "myon.string.concat expects two str values");
        }
        *out = eval_binary(it, line, OP_ADD, a, b); return 1;
    }
    if (strcmp(name, "myon.string.contains") == 0) {
        Value a = eval_arg(it, env, call, 0), b = eval_arg(it, env, call, 1);
        if (a.type != TYPE_STR || b.type != TYPE_STR) {
            value_free(&a); value_free(&b);
            runtime_error(it, line, "myon.string.contains expects two str values");
        }
        int found = strstr(a.as.obj->as.str, b.as.obj->as.str) != NULL;
        value_free(&a); value_free(&b);
        *out = value_bool(found); return 1;
    }
    if (strcmp(name, "myon.string.upper") == 0 || strcmp(name, "myon.string.lower") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type != TYPE_STR) { value_free(&a); runtime_error(it, line, "expects str"); }
        int up = (name[strlen(name)-1] == 'r' && strstr(name, "upper"));
        char *s = myon_strdup(a.as.obj->as.str);
        for (char *c = s; *c; c++)
            *c = up ? (char)toupper((unsigned char)*c) : (char)tolower((unsigned char)*c);
        value_free(&a);
        *out = value_str(s); return 1;
    }

    /* ---- myon.string (Phase3.5 extension, Step4) ----
     * Index-based helpers operate on *character* (code-point) positions, not
     * byte offsets, so they stay correct for multi-byte UTF-8 text. */

    /* substring(s, start, len) ret str, error — character-based slice. */
    if (strcmp(name, "myon.string.substring") == 0) {
        Value s = eval_arg(it, env, call, 0);
        Value vs = eval_arg(it, env, call, 1);
        Value vl = eval_arg(it, env, call, 2);
        if (s.type != TYPE_STR || vs.type != TYPE_INT || vl.type != TYPE_INT) {
            value_free(&s); value_free(&vs); value_free(&vl);
            runtime_error(it, line, "myon.string.substring expects (str, int, int)");
        }
        long long start = vs.as.i, len = vl.as.i;
        const char *cs = s.as.obj->as.str;
        long long nchars = utf8_char_count(cs);
        if (start < 0 || len < 0 || start + len > nchars) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("myon.string.substring: range out of bounds")));
            value_free(&s); value_free(&vs); value_free(&vl); return 1;
        }
        long long boff = utf8_byte_offset(cs, start);
        long long eoff = utf8_byte_offset(cs, start + len);
        long long nbytes = eoff - boff;
        char *buf = (char *)myon_xmalloc((size_t)nbytes + 1);
        memcpy(buf, cs + boff, (size_t)nbytes);
        buf[nbytes] = '\0';
        *out = make_result_pair(value_str(buf), value_nil());
        value_free(&s); value_free(&vs); value_free(&vl); return 1;
    }

    /* split(s, sep) ret myon.array(str). Empty sep splits into single chars. */
    if (strcmp(name, "myon.string.split") == 0) {
        Value s = eval_arg(it, env, call, 0), sep = eval_arg(it, env, call, 1);
        if (s.type != TYPE_STR || sep.type != TYPE_STR) {
            value_free(&s); value_free(&sep);
            runtime_error(it, line, "myon.string.split expects two str values");
        }
        const char *cs = s.as.obj->as.str;
        const char *csep = sep.as.obj->as.str;
        Value arr = value_array(typespec_prim(TYPE_STR));
        if (cs[0] == '\0') {
            /* Empty input yields an empty array. */
        } else if (csep[0] == '\0') {
            /* Empty separator: one element per code point. */
            const char *p = cs;
            while (*p) {
                long long adv = utf8_byte_offset(p, 1);
                if (adv <= 0) adv = 1; /* defensive: always make progress */
                char *piece = (char *)myon_xmalloc((size_t)adv + 1);
                memcpy(piece, p, (size_t)adv);
                piece[adv] = '\0';
                array_push(&arr, value_str(piece));
                p += adv;
            }
        } else {
            size_t seplen = strlen(csep);
            const char *p = cs;
            const char *hit;
            while ((hit = strstr(p, csep)) != NULL) {
                size_t n = (size_t)(hit - p);
                char *piece = (char *)myon_xmalloc(n + 1);
                memcpy(piece, p, n);
                piece[n] = '\0';
                array_push(&arr, value_str(piece));
                p = hit + seplen;
            }
            array_push(&arr, value_str(myon_strdup(p)));
        }
        value_free(&s); value_free(&sep);
        *out = arr; return 1;
    }

    /* join(parts, sep) ret str. */
    if (strcmp(name, "myon.string.join") == 0) {
        Value parts = eval_arg(it, env, call, 0), sep = eval_arg(it, env, call, 1);
        if (parts.type != TYPE_ARRAY || sep.type != TYPE_STR) {
            value_free(&parts); value_free(&sep);
            runtime_error(it, line, "myon.string.join expects (myon.array(str), str)");
        }
        const char *csep = sep.as.obj->as.str;
        size_t seplen = strlen(csep);
        ArrayData *a = &parts.as.obj->as.arr;
        size_t total = 1;
        for (int i = 0; i < a->count; i++) {
            if (a->items[i].type != TYPE_STR) {
                value_free(&parts); value_free(&sep);
                runtime_error(it, line, "myon.string.join: array elements must be str");
            }
            total += strlen(a->items[i].as.obj->as.str);
            if (i) total += seplen;
        }
        char *buf = (char *)myon_xmalloc(total);
        buf[0] = '\0';
        size_t off = 0;
        for (int i = 0; i < a->count; i++) {
            if (i) { memcpy(buf + off, csep, seplen); off += seplen; }
            const char *piece = a->items[i].as.obj->as.str;
            size_t plen = strlen(piece);
            memcpy(buf + off, piece, plen); off += plen;
        }
        buf[off] = '\0';
        value_free(&parts); value_free(&sep);
        *out = value_str(buf); return 1;
    }

    /* trim(s) ret str — strip leading/trailing ASCII whitespace. */
    if (strcmp(name, "myon.string.trim") == 0) {
        Value s = eval_arg(it, env, call, 0);
        if (s.type != TYPE_STR) { value_free(&s); runtime_error(it, line, "myon.string.trim expects str"); }
        const char *cs = s.as.obj->as.str;
        const char *b = cs;
        while (*b && isspace((unsigned char)*b)) b++;
        const char *e = cs + strlen(cs);
        while (e > b && isspace((unsigned char)e[-1])) e--;
        size_t n = (size_t)(e - b);
        char *buf = (char *)myon_xmalloc(n + 1);
        memcpy(buf, b, n); buf[n] = '\0';
        value_free(&s);
        *out = value_str(buf); return 1;
    }

    /* replace(s, from, to) ret str — replace every occurrence. */
    if (strcmp(name, "myon.string.replace") == 0) {
        Value s = eval_arg(it, env, call, 0);
        Value from = eval_arg(it, env, call, 1);
        Value to = eval_arg(it, env, call, 2);
        if (s.type != TYPE_STR || from.type != TYPE_STR || to.type != TYPE_STR) {
            value_free(&s); value_free(&from); value_free(&to);
            runtime_error(it, line, "myon.string.replace expects three str values");
        }
        const char *cs = s.as.obj->as.str;
        const char *cf = from.as.obj->as.str;
        const char *ct = to.as.obj->as.str;
        if (cf[0] == '\0') {
            /* Empty needle: return the input unchanged (avoid infinite loop). */
            value_free(&from); value_free(&to);
            *out = s; return 1;
        }
        size_t flen = strlen(cf), tlen = strlen(ct);
        size_t cap = strlen(cs) + 1, len = 0;
        char *buf = (char *)myon_xmalloc(cap);
        const char *p = cs;
        const char *hit;
        while ((hit = strstr(p, cf)) != NULL) {
            size_t chunk = (size_t)(hit - p);
            while (len + chunk + tlen + 1 > cap) { cap *= 2; buf = (char *)myon_xrealloc(buf, cap); }
            memcpy(buf + len, p, chunk); len += chunk;
            memcpy(buf + len, ct, tlen); len += tlen;
            p = hit + flen;
        }
        size_t rest = strlen(p);
        while (len + rest + 1 > cap) { cap *= 2; buf = (char *)myon_xrealloc(buf, cap); }
        memcpy(buf + len, p, rest); len += rest;
        buf[len] = '\0';
        value_free(&s); value_free(&from); value_free(&to);
        *out = value_str(buf); return 1;
    }

    /* index_of(s, sub) ret int — character index of first match, or -1. */
    if (strcmp(name, "myon.string.index_of") == 0) {
        Value s = eval_arg(it, env, call, 0), sub = eval_arg(it, env, call, 1);
        if (s.type != TYPE_STR || sub.type != TYPE_STR) {
            value_free(&s); value_free(&sub);
            runtime_error(it, line, "myon.string.index_of expects two str values");
        }
        const char *cs = s.as.obj->as.str;
        const char *hit = strstr(cs, sub.as.obj->as.str);
        long long idx;
        if (!hit) {
            idx = -1;
        } else {
            /* Convert the byte offset of the match to a character index. */
            size_t boff = (size_t)(hit - cs);
            char saved = cs[boff];
            ((char *)cs)[boff] = '\0';
            idx = utf8_char_count(cs);
            ((char *)cs)[boff] = saved;
        }
        value_free(&s); value_free(&sub);
        *out = value_int(idx); return 1;
    }

    /* starts_with / ends_with ret bool. */
    if (strcmp(name, "myon.string.starts_with") == 0) {
        Value s = eval_arg(it, env, call, 0), pre = eval_arg(it, env, call, 1);
        if (s.type != TYPE_STR || pre.type != TYPE_STR) {
            value_free(&s); value_free(&pre);
            runtime_error(it, line, "myon.string.starts_with expects two str values");
        }
        const char *cs = s.as.obj->as.str;
        const char *cp = pre.as.obj->as.str;
        size_t plen = strlen(cp);
        int r = strncmp(cs, cp, plen) == 0;
        value_free(&s); value_free(&pre);
        *out = value_bool(r); return 1;
    }
    if (strcmp(name, "myon.string.ends_with") == 0) {
        Value s = eval_arg(it, env, call, 0), suf = eval_arg(it, env, call, 1);
        if (s.type != TYPE_STR || suf.type != TYPE_STR) {
            value_free(&s); value_free(&suf);
            runtime_error(it, line, "myon.string.ends_with expects two str values");
        }
        const char *cs = s.as.obj->as.str;
        const char *cx = suf.as.obj->as.str;
        size_t slen = strlen(cs), xlen = strlen(cx);
        int r = xlen <= slen && memcmp(cs + slen - xlen, cx, xlen) == 0;
        value_free(&s); value_free(&suf);
        *out = value_bool(r); return 1;
    }

    /* repeat(s, n) ret str, error — n<0 errors, n==0 yields "". */
    if (strcmp(name, "myon.string.repeat") == 0) {
        Value s = eval_arg(it, env, call, 0), vn = eval_arg(it, env, call, 1);
        if (s.type != TYPE_STR || vn.type != TYPE_INT) {
            value_free(&s); value_free(&vn);
            runtime_error(it, line, "myon.string.repeat expects (str, int)");
        }
        long long n = vn.as.i;
        if (n < 0) {
            *out = make_result_pair(value_str(myon_strdup("")),
                value_error(myon_strdup("myon.string.repeat: count must be non-negative")));
            value_free(&s); value_free(&vn); return 1;
        }
        const char *cs = s.as.obj->as.str;
        size_t unit = strlen(cs);
        size_t total = unit * (size_t)n;
        char *buf = (char *)myon_xmalloc(total + 1);
        for (long long i = 0; i < n; i++) memcpy(buf + (size_t)i * unit, cs, unit);
        buf[total] = '\0';
        *out = make_result_pair(value_str(buf), value_nil());
        value_free(&s); value_free(&vn); return 1;
    }

    /* to_int(s) ret int, error. */
    if (strcmp(name, "myon.string.to_int") == 0) {
        Value s = eval_arg(it, env, call, 0);
        if (s.type != TYPE_STR) { value_free(&s); runtime_error(it, line, "myon.string.to_int expects str"); }
        const char *cs = s.as.obj->as.str;
        char *end = NULL;
        errno = 0;
        long long v = strtoll(cs, &end, 10);
        if (cs[0] == '\0' || end == cs || *end != '\0') {
            *out = make_result_pair(value_int(0),
                value_error(myon_strdup("myon.string.to_int: not an integer")));
        } else {
            *out = make_result_pair(value_int(v), value_nil());
        }
        value_free(&s); return 1;
    }

    /* to_float(s) ret float, error. */
    if (strcmp(name, "myon.string.to_float") == 0) {
        Value s = eval_arg(it, env, call, 0);
        if (s.type != TYPE_STR) { value_free(&s); runtime_error(it, line, "myon.string.to_float expects str"); }
        const char *cs = s.as.obj->as.str;
        char *end = NULL;
        errno = 0;
        double v = strtod(cs, &end);
        if (cs[0] == '\0' || end == cs || *end != '\0') {
            *out = make_result_pair(value_float(0.0),
                value_error(myon_strdup("myon.string.to_float: not a number")));
        } else {
            *out = make_result_pair(value_float(v), value_nil());
        }
        value_free(&s); return 1;
    }

    /* from_int / from_float ret str — reuse value_to_cstr()'s rendering. */
    if (strcmp(name, "myon.string.from_int") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type != TYPE_INT) { value_free(&a); runtime_error(it, line, "myon.string.from_int expects int"); }
        *out = value_str(value_to_cstr(&a)); value_free(&a); return 1;
    }
    if (strcmp(name, "myon.string.from_float") == 0) {
        Value a = eval_arg(it, env, call, 0);
        if (a.type != TYPE_FLOAT) { value_free(&a); runtime_error(it, line, "myon.string.from_float expects float"); }
        *out = value_str(value_to_cstr(&a)); value_free(&a); return 1;
    }

    (void)argc;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Method dispatch on arrays / maps / structs                          */
/* ------------------------------------------------------------------ */

static int typespec_matches_value(Interp *it, TypeSpec *ts, const Value *v);

static Value call_method(Interp *it, Env *env, Expr *call, Value recv,
                         const char *method) {
    int line = call->line;
    int argc = call->as.call.arg_count;

    if (recv.type == TYPE_ARRAY) {
        ArrayData *a = &recv.as.obj->as.arr;
        if (strcmp(method, "push") == 0) {
            if (argc != 1) runtime_error(it, line, "push expects 1 argument");
            Value v = eval_expr(it, env, call->as.call.args[0]);
            if (a->elem_type && !typespec_matches_value(it, a->elem_type, &v)) {
                char *want = typespec_to_cstr(a->elem_type);
                const char *got = value_type_name(&v);
                value_free(&v);
                runtime_error(it, line,
                    "type mismatch: cannot push %s into myon.array(%s)", got, want);
            }
            array_push(&recv, v);
            return value_void();
        }
        if (strcmp(method, "pop") == 0) {
            Value out;
            if (!array_pop(&recv, &out))
                runtime_error(it, line, "pop from empty array");
            return out;
        }
        if (strcmp(method, "length") == 0) {
            return value_int(a->count);
        }
        runtime_error(it, line, "unknown array method '%s'", method);
    }

    if (recv.type == TYPE_MAP) {
        if (strcmp(method, "set") == 0) {
            Value k = eval_expr(it, env, call->as.call.args[0]);
            Value v = eval_expr(it, env, call->as.call.args[1]);
            map_set(&recv, k, v);
            return value_void();
        }
        if (strcmp(method, "get") == 0) {
            Value k = eval_expr(it, env, call->as.call.args[0]);
            Value out;
            if (!map_get(&recv, &k, &out)) { value_free(&k); return value_nil(); }
            value_free(&k);
            return out;
        }
        if (strcmp(method, "has") == 0) {
            Value k = eval_expr(it, env, call->as.call.args[0]);
            int has = map_has(&recv, &k);
            value_free(&k);
            return value_bool(has);
        }
        if (strcmp(method, "delete") == 0) {
            Value k = eval_expr(it, env, call->as.call.args[0]);
            int ok = map_delete(&recv, &k);
            value_free(&k);
            return value_bool(ok);
        }
        runtime_error(it, line, "unknown map method '%s'", method);
    }

    if (recv.type == TYPE_STRUCT) {
        StructDecl *sd = recv.as.obj->as.st.decl;
        /* search up the inheritance chain for the method */
        FuncDecl *m = NULL;
        for (StructDecl *cur = sd; cur && !m; cur = cur->parent) {
            for (int i = 0; i < cur->method_count; i++)
                if (strcmp(cur->methods[i]->name, method) == 0) { m = cur->methods[i]; break; }
        }
        if (!m)
            runtime_error(it, line, "struct '%s' has no method '%s'",
                          recv.as.obj->as.st.type_name, method);

        /* evaluate arguments */
        Value *args = argc ? (Value *)myon_xmalloc(sizeof(Value) * argc) : NULL;
        for (int i = 0; i < argc; i++)
            args[i] = eval_expr(it, env, call->as.call.args[i]);

        /* bind a function value with self = recv */
        Value fnv = value_func(m, it->global);
        fnv.as.obj->as.fn.is_bound = 1;
        fnv.as.obj->as.fn.bound_self = (Value *)myon_xmalloc(sizeof(Value));
        *fnv.as.obj->as.fn.bound_self = value_copy(&recv);

        Value r = call_function(it, line, fnv, args, argc, NULL);
        value_free(&fnv);
        for (int i = 0; i < argc; i++) value_free(&args[i]);
        free(args);
        return r;
    }

    runtime_error(it, line, "type %s has no methods", value_type_name(&recv));
    return value_nil();
}

/* ------------------------------------------------------------------ */
/* Struct construction (StructName(field=..., ...))                    */
/* ------------------------------------------------------------------ */

/* collect fields from the whole inheritance chain (parent first) */
static void collect_fields(StructDecl *sd, StructField **out, int *count) {
    if (!sd) return;
    collect_fields(sd->parent, out, count);
    for (int i = 0; i < sd->field_count; i++) {
        *out = (StructField *)myon_xrealloc(*out, sizeof(StructField) * (*count + 1));
        (*out)[(*count)++] = sd->fields[i];
    }
}

static Value construct_struct(Interp *it, Env *env, Expr *call, StructDecl *sd,
                              TypeSpec **type_args, int type_arg_count) {
    int line = call->line;
    Value sv = value_struct(sd->name, sd);

    /* record generic bindings on the instance (Step 15) */
    if (type_arg_count > 0) {
        StructData *st = &sv.as.obj->as.st;
        st->tparam_count = type_arg_count;
        st->tparam_names = (char **)myon_xmalloc(sizeof(char *) * type_arg_count);
        st->tparam_types = (TypeSpec **)myon_xmalloc(sizeof(TypeSpec *) * type_arg_count);
        for (int i = 0; i < type_arg_count && i < sd->tparam_count; i++) {
            st->tparam_names[i] = myon_strdup(sd->tparams[i]);
            st->tparam_types[i] = typespec_clone(type_args[i]);
        }
        /* fill remaining names if fewer args than params */
        for (int i = sd->tparam_count; i < type_arg_count; i++) {
            st->tparam_names[i] = myon_strdup("?");
            st->tparam_types[i] = typespec_clone(type_args[i]);
        }
    }

    StructField *fields = NULL;
    int fcount = 0;
    collect_fields(sd, &fields, &fcount);

    /* initialise every field, matching named arguments */
    for (int i = 0; i < fcount; i++) {
        Value fv = value_nil();
        int found = 0;
        for (int j = 0; j < call->as.call.arg_count; j++) {
            if (call->as.call.arg_names[j] &&
                strcmp(call->as.call.arg_names[j], fields[i].name) == 0) {
                fv = eval_expr(it, env, call->as.call.args[j]);
                found = 1;
                break;
            }
        }
        if (!found) {
            free(fields);
            value_free(&sv);
            runtime_error(it, line, "missing field '%s' in %s(...) construction",
                          fields[i].name, sd->name);
        }
        struct_add_field(&sv, fields[i].name, fv);
    }
    free(fields);
    return sv;
}

/* ------------------------------------------------------------------ */
/* typespec / value compatibility                                      */
/* ------------------------------------------------------------------ */

static int typespec_matches_value(Interp *it, TypeSpec *ts, const Value *v) {
    (void)it;
    if (!ts) return 1;
    switch (ts->base) {
        case TYPE_INT:    return v->type == TYPE_INT;
        case TYPE_FLOAT:  return v->type == TYPE_FLOAT;
        case TYPE_STR:    return v->type == TYPE_STR;
        case TYPE_CHAR:   return v->type == TYPE_CHAR;
        case TYPE_BOOL:   return v->type == TYPE_BOOL;
        case TYPE_VOID:   return v->type == TYPE_VOID;
        case TYPE_ERROR:  return v->type == TYPE_ERROR || v->type == TYPE_NIL;
        case TYPE_ARRAY:  return v->type == TYPE_ARRAY;
        case TYPE_MAP:    return v->type == TYPE_MAP;
        case TYPE_STRUCT:
            /* a bare identifier annotation: struct name OR a generic
             * type parameter — accept structurally by name, else accept any
             * (type parameters are erased at runtime). */
            if (v->type == TYPE_STRUCT)
                return strcmp(ts->name, v->as.obj->as.st.type_name) == 0;
            /* unknown struct name -> treat as generic param, accept anything */
            return 1;
        default:          return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Function invocation (Step 6 + closures)                             */
/* ------------------------------------------------------------------ */

static Value call_function(Interp *it, int line, Value fn, Value *args, int argc,
                           char **arg_names) {
    (void)arg_names;
    if (fn.type != TYPE_FUNC)
        runtime_error(it, line, "attempt to call a non-function value (%s)", type_name(fn.type));

    FuncDecl *fd = fn.as.obj->as.fn.decl;
    if (argc != fd->param_count)
        runtime_error(it, line, "function '%s' expects %d argument(s), got %d",
                      fd->name ? fd->name : "<lambda>", fd->param_count, argc);

    Env *call_env = env_new(fn.as.obj->as.fn.closure);

    /* bind self for methods */
    if (fn.as.obj->as.fn.is_bound && fn.as.obj->as.fn.bound_self) {
        env_define(call_env, "self", value_copy(fn.as.obj->as.fn.bound_self));
    }

    /* bind parameters with type-checking (spec 6.1: annotations required) */
    for (int i = 0; i < argc; i++) {
        if (!typespec_matches_value(it, fd->params[i].type, &args[i])) {
            char *want = typespec_to_cstr(fd->params[i].type);
            const char *got = value_type_name(&args[i]);
            env_free(call_env);
            runtime_error(it, line,
                "argument %d of '%s' expects %s, got %s",
                i + 1, fd->name ? fd->name : "<lambda>", want, got);
        }
        env_define(call_env, fd->params[i].name, value_copy(&args[i]));
    }

    /* run body */
    Value *saved_ret = it->ret_values;
    int    saved_cnt = it->ret_count;
    it->ret_values = NULL;
    it->ret_count = 0;

    Flow f = exec_block(it, call_env, fd->body);

    Value result;
    if (f == FLOW_RETURN) {
        if (it->ret_count == 0) {
            result = value_void();
        } else if (it->ret_count == 1) {
            result = it->ret_values[0];
        } else {
            /* multiple returns -> pack into an array-like tuple.
             * We represent the tuple with a special array so multi-assignment
             * can unpack it. */
            Value tup = value_array(NULL);
            for (int i = 0; i < it->ret_count; i++)
                array_push(&tup, it->ret_values[i]);
            result = tup;
        }
        free(it->ret_values);
    } else {
        result = value_void();
    }

    it->ret_values = saved_ret;
    it->ret_count = saved_cnt;

    env_free(call_env);
    return result;
}

/* ------------------------------------------------------------------ */
/* Expression evaluation                                               */
/* ------------------------------------------------------------------ */

static Value eval_call(Interp *it, Env *env, Expr *e) {
    Expr *callee = e->as.call.callee;
    int line = e->line;

    /* builtin ident callees */
    if (callee->kind == EXPR_IDENT) {
        const char *name = callee->as.ident;
        if (strcmp(name, "myon.print") == 0) return builtin_print(it, env, e);
        if (strcmp(name, "myon.input") == 0) return builtin_input(it, env, e);
        /* stdlib namespaced calls (myon.math.* / myon.string.*) */
        if (strncmp(name, "myon.", 5) == 0) {
            Value out;
            if (call_stdlib(it, env, name, e, &out)) return out;
        }
        /* struct constructor: Name(field=...) */
        StructDecl *sd = find_struct(it, name);
        if (sd)
            return construct_struct(it, env, e, sd, NULL, 0);
    }

    /* generic constructor: Name<T>(...) */
    if (callee->kind == EXPR_GENERIC) {
        StructDecl *sd = find_struct(it, callee->as.generic.name);
        if (sd)
            return construct_struct(it, env, e, sd,
                                    callee->as.generic.args,
                                    callee->as.generic.arg_count);
        runtime_error(it, line, "unknown generic type '%s'", callee->as.generic.name);
    }

    /* method call: obj.method(args) */
    if (callee->kind == EXPR_MEMBER) {
        Value recv = eval_expr(it, env, callee->as.member.target);
        Value r = call_method(it, env, e, recv, callee->as.member.name);
        value_free(&recv);
        return r;
    }

    /* first-class function value (variable holding a func / lambda) */
    Value fn = eval_expr(it, env, callee);
    if (fn.type == TYPE_FUNC) {
        int argc = e->as.call.arg_count;
        Value *args = argc ? (Value *)myon_xmalloc(sizeof(Value) * argc) : NULL;
        for (int i = 0; i < argc; i++)
            args[i] = eval_expr(it, env, e->as.call.args[i]);
        Value r = call_function(it, line, fn, args, argc, e->as.call.arg_names);
        for (int i = 0; i < argc; i++) value_free(&args[i]);
        free(args);
        value_free(&fn);
        return r;
    }
    value_free(&fn);
    runtime_error(it, line, "call of unsupported callee");
    return value_nil();
}

static Value eval_expr(Interp *it, Env *env, Expr *e) {
    switch (e->kind) {
        case EXPR_INT_LIT:   return value_int(e->as.int_val);
        case EXPR_FLOAT_LIT: return value_float(e->as.float_val);
        case EXPR_BOOL_LIT:  return value_bool(e->as.bool_val);
        case EXPR_STRING:    return interpolate_string(it, env, e->line, e->as.str_val);
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
            if (v.type != TYPE_STR) {
                value_free(&v);
                runtime_error(it, e->line, "error() expects a str argument");
            }
            char *msg = myon_strdup(v.as.obj->as.str);
            value_free(&v);
            return value_error(msg);
        }

        case EXPR_ARRAY_CTOR:
            return value_array(typespec_clone(e->as.array_elem));
        case EXPR_MAP_CTOR:
            return value_map(typespec_clone(e->as.map_types.key),
                             typespec_clone(e->as.map_types.val));

        case EXPR_LAMBDA:
            /* capture the current environment as the closure */
            return value_func(e->as.lambda, env);

        case EXPR_AWAIT: {
            /* pseudo-async: await simply evaluates the (already-run) value */
            return eval_expr(it, env, e->as.operand);
        }

        case EXPR_GENERIC: {
            /* a generic name used without a call — treat as a struct ctor ref
             * is invalid here; only meaningful inside a call. */
            runtime_error(it, e->line, "generic type '%s' must be instantiated with (...)",
                          e->as.generic.name);
            return value_nil();
        }

        case EXPR_UNARY: {
            Value v = eval_expr(it, env, e->as.unary.operand);
            if (e->as.unary.op == OP_NEG) {
                if (v.type == TYPE_INT)   return value_int(-v.as.i);
                if (v.type == TYPE_FLOAT) return value_float(-v.as.f);
                Type t = v.type; value_free(&v);
                runtime_error(it, e->line, "unary '-' requires int or float, got %s", type_name(t));
            } else {
                if (v.type != TYPE_BOOL) {
                    Type t = v.type; value_free(&v);
                    runtime_error(it, e->line, "myon.not requires bool, got %s", type_name(t));
                }
                return value_bool(!v.as.b);
            }
            return value_nil();
        }

        case EXPR_LOGICAL: {
            Value l = eval_expr(it, env, e->as.binary.left);
            if (l.type != TYPE_BOOL) {
                Type t = l.type; value_free(&l);
                runtime_error(it, e->line, "logical operator requires bool, got %s", type_name(t));
            }
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

        case EXPR_CALL:
            return eval_call(it, env, e);

        case EXPR_INDEX: {
            Value target = eval_expr(it, env, e->as.index.target);
            Value idx = eval_expr(it, env, e->as.index.index);
            if (target.type == TYPE_ARRAY) {
                if (idx.type != TYPE_INT) {
                    value_free(&target); value_free(&idx);
                    runtime_error(it, e->line, "array index must be int");
                }
                ArrayData *a = &target.as.obj->as.arr;
                long long i = idx.as.i;
                if (i < 0 || i >= a->count) {
                    value_free(&target); value_free(&idx);
                    runtime_error(it, e->line, "array index %lld out of bounds (length %d)", i, a->count);
                }
                Value r = value_copy(&a->items[i]);
                value_free(&target); value_free(&idx);
                return r;
            }
            if (target.type == TYPE_MAP) {
                Value out;
                int ok = map_get(&target, &idx, &out);
                value_free(&target); value_free(&idx);
                if (!ok) return value_nil();
                return out;
            }
            value_free(&target); value_free(&idx);
            runtime_error(it, e->line, "cannot index type");
            return value_nil();
        }

        case EXPR_MEMBER: {
            Value target = eval_expr(it, env, e->as.member.target);
            if (target.type == TYPE_STRUCT) {
                Value *fp = struct_field_ptr(&target, e->as.member.name);
                if (!fp) {
                    const char *tn = target.as.obj->as.st.type_name;
                    value_free(&target);
                    runtime_error(it, e->line, "struct '%s' has no field '%s'",
                                  tn, e->as.member.name);
                }
                Value r = value_copy(fp);
                value_free(&target);
                return r;
            }
            value_free(&target);
            runtime_error(it, e->line, "member access on non-struct value");
            return value_nil();
        }
    }
    runtime_error(it, e->line, "unknown expression kind");
    return value_nil();
}

/* ------------------------------------------------------------------ */
/* Assignment to member / index targets                                */
/* ------------------------------------------------------------------ */

static void assign_target(Interp *it, Env *env, Expr *target, Value v) {
    int line = target->line;
    if (target->kind == EXPR_MEMBER) {
        Value obj = eval_expr(it, env, target->as.member.target);
        if (obj.type != TYPE_STRUCT) {
            value_free(&obj); value_free(&v);
            runtime_error(it, line, "cannot set field on non-struct");
        }
        Value *fp = struct_field_ptr(&obj, target->as.member.name);
        if (!fp) {
            const char *tn = obj.as.obj->as.st.type_name;
            value_free(&obj); value_free(&v);
            runtime_error(it, line, "struct '%s' has no field '%s'", tn, target->as.member.name);
        }
        value_free(fp);
        *fp = v;               /* obj shares the same Obj, mutation is visible */
        value_free(&obj);
        return;
    }
    if (target->kind == EXPR_INDEX) {
        Value obj = eval_expr(it, env, target->as.index.target);
        Value idx = eval_expr(it, env, target->as.index.index);
        if (obj.type == TYPE_ARRAY) {
            if (idx.type != TYPE_INT) { value_free(&obj); value_free(&idx); value_free(&v); runtime_error(it, line, "array index must be int"); }
            ArrayData *a = &obj.as.obj->as.arr;
            long long i = idx.as.i;
            if (i < 0 || i >= a->count) { value_free(&obj); value_free(&idx); value_free(&v); runtime_error(it, line, "array index out of bounds"); }
            value_free(&a->items[i]);
            a->items[i] = v;
            value_free(&obj); value_free(&idx);
            return;
        }
        if (obj.type == TYPE_MAP) {
            map_set(&obj, idx, v);   /* takes ownership of idx and v */
            value_free(&obj);
            return;
        }
        value_free(&obj); value_free(&idx); value_free(&v);
        runtime_error(it, line, "cannot index-assign to this type");
    }
    value_free(&v);
    runtime_error(it, line, "invalid assignment target");
}

/* ------------------------------------------------------------------ */
/* Statement execution                                                 */
/* ------------------------------------------------------------------ */

static Flow do_multi_assign(Interp *it, Env *env, Stmt *s, Value v) {
    /* v must be a tuple (array with NULL elem_type) from a multi-return */
    int total = 1 + s->as.assign.extra_count;
    if (v.type != TYPE_ARRAY || v.as.obj->as.arr.elem_type != NULL) {
        value_free(&v);
        runtime_error(it, s->line,
            "multiple-target assignment requires a multi-value return (spec 6.2)");
    }
    ArrayData *a = &v.as.obj->as.arr;
    if (a->count != total) {
        value_free(&v);
        runtime_error(it, s->line,
            "assignment target count (%d) does not match return count (%d)",
            total, a->count);
    }
    /* names[0] = name, names[1..] = extra_names */
    for (int i = 0; i < total; i++) {
        const char *nm = (i == 0) ? s->as.assign.name : s->as.assign.extra_names[i - 1];
        Value elem = value_copy(&a->items[i]);
        if (env_defined_local(env, nm)) env_set(env, nm, elem);
        else                            env_define(env, nm, elem);
    }
    value_free(&v);
    return FLOW_NORMAL;
}

static Flow exec_stmt(Interp *it, Env *env, Stmt *s) {
    switch (s->kind) {
        case STMT_SYSTEM:
            return FLOW_NORMAL;
        case STMT_MODULE:
            /* module loading handled in interpret() preamble; no-op here */
            return FLOW_NORMAL;

        case STMT_FUNC: {
            const char *nm = s->as.func->name;
            /* Top-level functions are hoisted during prescan; when we reach
             * the declaration statement again it is already bound to exactly
             * this decl, so treat it as a no-op. A nested function declaration
             * (inside a block/loop/function) is defined here on first sight. */
            if (env_defined_local(env, nm)) {
                Value existing;
                env_get(env, nm, &existing);
                int same = (existing.type == TYPE_FUNC &&
                            existing.as.obj->as.fn.decl == s->as.func);
                value_free(&existing);
                if (same) return FLOW_NORMAL;
                runtime_error(it, s->line,
                    "redefinition of '%s' (shadowing is forbidden, spec 9.2)", nm);
            }
            env_define(env, nm, value_func(s->as.func, env));
            return FLOW_NORMAL;
        }

        case STMT_STRUCT:
            /* registration is done in the preamble; ignore here */
            return FLOW_NORMAL;

        case STMT_RETURN: {
            int n = s->as.ret.count;
            it->ret_values = n ? (Value *)myon_xmalloc(sizeof(Value) * n) : NULL;
            it->ret_count = n;
            for (int i = 0; i < n; i++)
                it->ret_values[i] = eval_expr(it, env, s->as.ret.values[i]);
            return FLOW_RETURN;
        }

        case STMT_ASSIGN: {
            /* target assignment (a.b = v / a[i] = v) */
            if (s->as.assign.target) {
                if (s->as.assign.is_compound) {
                    Value cur = eval_expr(it, env, s->as.assign.target);
                    Value rhs = eval_expr(it, env, s->as.assign.value);
                    Value res = eval_binary(it, s->line, s->as.assign.compound, cur, rhs);
                    assign_target(it, env, s->as.assign.target, res);
                } else {
                    Value v = eval_expr(it, env, s->as.assign.value);
                    assign_target(it, env, s->as.assign.target, v);
                }
                return FLOW_NORMAL;
            }

            Value v = eval_expr(it, env, s->as.assign.value);

            if (s->as.assign.is_compound) {
                Value cur;
                if (!env_get(env, s->as.assign.name, &cur)) {
                    value_free(&v);
                    runtime_error(it, s->line, "compound assignment to undefined variable '%s'",
                                  s->as.assign.name);
                }
                Value res = eval_binary(it, s->line, s->as.assign.compound, cur, v);
                if (!env_set(env, s->as.assign.name, res))
                    runtime_error(it, s->line, "failed to assign '%s'", s->as.assign.name);
                return FLOW_NORMAL;
            }

            /* multiple-target assignment */
            if (s->as.assign.extra_count > 0)
                return do_multi_assign(it, env, s, v);

            if (v.type == TYPE_NIL) {
                value_free(&v);
                runtime_error(it, s->line,
                    "cannot assign myon.nil to a normal variable (spec 2.4)");
            }

            if (s->as.assign.has_type &&
                !typespec_matches_value(it, s->as.assign.annotated, &v)) {
                char *want = typespec_to_cstr(s->as.assign.annotated);
                const char *got = value_type_name(&v);
                value_free(&v);
                runtime_error(it, s->line,
                    "type annotation mismatch: '%s' declared %s but value is %s",
                    s->as.assign.name, want, got);
            }

            /* Assignment / shadowing rules (spec 9.2):
             *  - re-assignment to a name already bound (locally or in an
             *    enclosing scope) updates that binding.
             *  - inside an explicit `{ }` block, introducing a name that
             *    already exists in an outer scope is forbidden (shadowing).
             */
            if (env_defined_local(env, s->as.assign.name)) {
                env_set(env, s->as.assign.name, v);
            } else {
                Value tmp;
                int outer = (env->parent && env_get(env->parent, s->as.assign.name, &tmp));
                if (outer) value_free(&tmp);
                if (outer && env->is_block) {
                    value_free(&v);
                    runtime_error(it, s->line,
                        "redefinition of '%s' shadows an outer variable (forbidden, spec 9.2)",
                        s->as.assign.name);
                }
                if (outer) {
                    /* assign through to the existing outer binding */
                    env_set(env, s->as.assign.name, v);
                } else {
                    env_define(env, s->as.assign.name, v);
                }
            }
            return FLOW_NORMAL;
        }

        case STMT_EXPR: {
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
            if (taken) {
                Env *sc = env_new(env);
                Flow f = exec_block(it, sc, &s->as.if_stmt.then_body);
                env_free(sc);
                return f;
            }
            for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
                Value ec = eval_expr(it, env, s->as.if_stmt.elifs[i].cond);
                if (ec.type != TYPE_BOOL) {
                    Type t = ec.type; value_free(&ec);
                    runtime_error(it, s->line, "elif condition must be bool, got %s", type_name(t));
                }
                if (ec.as.b) {
                    Env *sc = env_new(env);
                    Flow f = exec_block(it, sc, &s->as.if_stmt.elifs[i].body);
                    env_free(sc);
                    return f;
                }
            }
            if (s->as.if_stmt.has_else) {
                Env *sc = env_new(env);
                Flow f = exec_block(it, sc, &s->as.if_stmt.else_body);
                env_free(sc);
                return f;
            }
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
                Env *sc = env_new(env);
                Flow f = exec_block(it, sc, &s->as.while_stmt.body);
                env_free(sc);
                if (f == FLOW_BREAK) break;
                if (f == FLOW_RETURN) return f;
            }
            return FLOW_NORMAL;
        }

        case STMT_FOR: {
            if (s->as.for_stmt.is_range) {
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
                    if (f == FLOW_RETURN) return f;
                }
                return FLOW_NORMAL;
            }
            /* for-in over an array (spec 5.2 / 7) */
            Value iter = eval_expr(it, env, s->as.for_stmt.iterable);
            if (iter.type != TYPE_ARRAY) {
                Type t = iter.type; value_free(&iter);
                runtime_error(it, s->line, "myon.for ... myon.in requires an array, got %s", type_name(t));
            }
            ArrayData *a = &iter.as.obj->as.arr;
            for (int i = 0; i < a->count; i++) {
                Env *loop = env_new(env);
                env_define(loop, s->as.for_stmt.var, value_copy(&a->items[i]));
                Flow f = exec_block(it, loop, &s->as.for_stmt.body);
                env_free(loop);
                if (f == FLOW_BREAK) break;
                if (f == FLOW_RETURN) { value_free(&iter); return f; }
            }
            value_free(&iter);
            return FLOW_NORMAL;
        }

        case STMT_BREAK:    return FLOW_BREAK;
        case STMT_CONTINUE: return FLOW_CONTINUE;

        case STMT_BLOCK: {
            Env *scope = env_new(env);
            scope->is_block = 1;
            Flow f = exec_block(it, scope, &s->as.block);
            env_free(scope);
            return f;
        }

        case STMT_EXPOSE: {
            /* copy the named binding from this scope into the parent (spec 9.1) */
            Value v;
            if (!env_get(env, s->as.expose_name, &v))
                runtime_error(it, s->line, "myon.expose: '%s' is not defined here",
                              s->as.expose_name);
            if (env->parent) {
                if (env_defined_local(env->parent, s->as.expose_name))
                    env_set(env->parent, s->as.expose_name, v);
                else
                    env_define(env->parent, s->as.expose_name, v);
            } else {
                value_free(&v);
            }
            return FLOW_NORMAL;
        }
    }
    return FLOW_NORMAL;
}

static Flow exec_block(Interp *it, Env *env, StmtList *body) {
    for (int i = 0; i < body->count; i++) {
        Flow f = exec_stmt(it, env, body->items[i]);
        if (f != FLOW_NORMAL) return f;
    }
    return FLOW_NORMAL;
}

/* ------------------------------------------------------------------ */
/* Module loading (Step 11)                                            */
/* ------------------------------------------------------------------ */

static const char *BUILTIN_MODULES[] = {
    "myon.stdio", "myon.math", "myon.string", "myon.ffi", NULL
};

static int is_builtin_module(const char *path) {
    for (int i = 0; BUILTIN_MODULES[i]; i++)
        if (strcmp(BUILTIN_MODULES[i], path) == 0) return 1;
    return 0;
}

static ModuleEntry *find_module(Interp *it, const char *path) {
    for (ModuleEntry *m = it->modules; m; m = m->next)
        if (strcmp(m->path, path) == 0) return m;
    return NULL;
}

/* register + pre-scan a program's top-level struct/function declarations */
static void prescan(Interp *it, Env *env, Program *prog);

static int load_external_module(Interp *it, Stmt *decl);

static void handle_module_decl(Interp *it, Stmt *s) {
    const char *path = s->as.module_decl.path;
    if (is_builtin_module(path)) {
        /* builtin: just record it */
        ModuleEntry *m = (ModuleEntry *)myon_xmalloc(sizeof(ModuleEntry));
        m->path = myon_strdup(path);
        m->alias = s->as.module_decl.alias ? myon_strdup(s->as.module_decl.alias) : NULL;
        m->loading = 0;
        m->next = it->modules;
        it->modules = m;
        return;
    }
    /* external module: external.util.math -> ./util/math.myon */
    load_external_module(it, s);
}

static int load_external_module(Interp *it, Stmt *decl) {
    const char *path = decl->as.module_decl.path;

    ModuleEntry *existing = find_module(it, path);
    if (existing) {
        if (existing->loading)
            runtime_error(it, decl->line, "circular module import detected: '%s' (spec 14.5)", path);
        return 1; /* already loaded */
    }

    ModuleEntry *m = (ModuleEntry *)myon_xmalloc(sizeof(ModuleEntry));
    m->path = myon_strdup(path);
    m->alias = decl->as.module_decl.alias ? myon_strdup(decl->as.module_decl.alias) : NULL;
    m->loading = 1;
    m->next = it->modules;
    it->modules = m;

    /* build file path: strip leading "external." then dots -> '/' + .myon */
    const char *p = path;
    if (strncmp(p, "external.", 9) == 0) p += 9;
    char file[512];
    size_t len = 0;
    file[0] = '\0';
    len += (size_t)snprintf(file + len, sizeof(file) - len, "./");
    for (const char *c = p; *c; c++)
        file[len++] = (*c == '.') ? '/' : *c;
    file[len] = '\0';
    snprintf(file + len, sizeof(file) - len, ".myon");

    FILE *f = fopen(file, "rb");
    if (!f)
        runtime_error(it, decl->line, "cannot open module file '%s' for '%s'", file, path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)myon_xmalloc((size_t)sz + 1);
    size_t n = fread(src, 1, (size_t)sz, f);
    src[n] = '\0';
    fclose(f);

    TokenList tl;
    if (!lexer_tokenize(src, &tl)) { free(src); runtime_error(it, decl->line, "lex error in module '%s'", path); }
    Program *mp = parser_parse(&tl);
    if (!mp) { token_list_free(&tl); free(src); runtime_error(it, decl->line, "parse error in module '%s'", path); }

    /* execute module top-level into the global scope (simple namespace model) */
    prescan(it, it->global, mp);
    for (int i = 0; i < mp->stmts.count; i++) {
        if (mp->stmts.items[i]->kind == STMT_MODULE)
            handle_module_decl(it, mp->stmts.items[i]);
        else
            exec_stmt(it, it->global, mp->stmts.items[i]);
    }

    /* NOTE: module AST is intentionally leaked for the lifetime of the run,
     * because function/struct values captured from it reference its nodes.
     * A production implementation would track and free these at shutdown. */
    (void)mp;
    token_list_free(&tl);
    free(src);

    m->loading = 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Pre-scan: hoist struct + function declarations                      */
/* ------------------------------------------------------------------ */

static void prescan(Interp *it, Env *env, Program *prog) {
    /* First pass: register structs so constructors/type refs resolve. */
    for (int i = 0; i < prog->stmts.count; i++) {
        Stmt *s = prog->stmts.items[i];
        if (s->kind == STMT_STRUCT) {
            StructDecl *sd = s->as.struct_decl;
            if (find_struct(it, sd->name))
                runtime_error(it, s->line, "redefinition of struct '%s'", sd->name);
            register_struct(it, sd);
        }
    }
    /* resolve parents + check field-name collisions (spec 14.6) */
    for (int i = 0; i < it->structs.count; i++) {
        StructDecl *sd = it->structs.items[i];
        if (sd->parent_name && !sd->parent) {
            sd->parent = find_struct(it, sd->parent_name);
            if (!sd->parent)
                runtime_error(it, 0, "struct '%s' extends unknown struct '%s'",
                              sd->name, sd->parent_name);
            /* field collision check */
            for (int a = 0; a < sd->field_count; a++)
                for (StructDecl *pc = sd->parent; pc; pc = pc->parent)
                    for (int b = 0; b < pc->field_count; b++)
                        if (strcmp(sd->fields[a].name, pc->fields[b].name) == 0)
                            runtime_error(it, 0,
                                "field '%s' in struct '%s' collides with parent (spec 14.6)",
                                sd->fields[a].name, sd->name);
        }
    }
    /* Second pass: hoist top-level functions so they can be mutually recursive. */
    for (int i = 0; i < prog->stmts.count; i++) {
        Stmt *s = prog->stmts.items[i];
        if (s->kind == STMT_FUNC) {
            const char *nm = s->as.func->name;
            if (!env_defined_local(env, nm))
                env_define(env, nm, value_func(s->as.func, env));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

Interp *interp_create(void) {
    Interp *it = (Interp *)myon_xmalloc(sizeof(Interp));
    memset(it, 0, sizeof(*it));
    it->global = env_new(NULL);
    return it;
}

void interp_free(Interp *it) {
    if (!it) return;
    env_free(it->global);
    free(it->structs.items);
    if (it->ffi) ffi_state_free(it->ffi);
    ModuleEntry *m = it->modules;
    while (m) { ModuleEntry *n = m->next; free(m->path); free(m->alias); free(m); m = n; }
    /* free retained program ASTs (structs/functions may reference their nodes,
     * so this only happens once the interpreter itself is torn down). */
    for (int i = 0; i < it->program_count; i++)
        program_free(it->programs[i]);
    free(it->programs);
    free(it);
}

/*
 * Execute a program's top-level statements on `it`'s global scope.  Assumes a
 * setjmp barrier for it->on_error has already been installed by the caller.
 * Returns 0 normally, 1 on the break/continue-outside-loop error.
 */
static int run_toplevel(Interp *it, Program *program) {
    /* load modules first (so external decls hoist) */
    for (int i = 0; i < program->stmts.count; i++)
        if (program->stmts.items[i]->kind == STMT_MODULE)
            handle_module_decl(it, program->stmts.items[i]);

    /* hoist struct/function declarations */
    prescan(it, it->global, program);

    for (int i = 0; i < program->stmts.count; i++) {
        Stmt *s = program->stmts.items[i];
        if (s->kind == STMT_MODULE) continue; /* already handled */
        Flow f = exec_stmt(it, it->global, s);
        if (f == FLOW_BREAK || f == FLOW_CONTINUE) {
            fprintf(stderr, "myon: runtime error: break/continue outside of a loop\n");
            return 1;
        }
        if (f == FLOW_RETURN) {
            /* top-level ret: ignore returned values */
            for (int k = 0; k < it->ret_count; k++) value_free(&it->ret_values[k]);
            free(it->ret_values);
            it->ret_values = NULL; it->ret_count = 0;
        }
    }
    return 0;
}

int interp_run(Interp *it, Program *program) {
    /* retain the AST for the interpreter's lifetime */
    it->programs = (Program **)myon_xrealloc(
        it->programs, sizeof(Program *) * (it->program_count + 1));
    it->programs[it->program_count++] = program;

    if (setjmp(it->on_error)) {
        /* a runtime error aborted this program; interpreter stays alive */
        return 1;
    }
    return run_toplevel(it, program);
}

int interpret(Program *program) {
    Interp it;
    memset(&it, 0, sizeof(it));
    it.global = env_new(NULL);

    int rc = 0;
    if (setjmp(it.on_error)) {
        rc = 1;
    } else {
        rc = run_toplevel(&it, program);
    }

    env_free(it.global);
    free(it.structs.items);
    if (it.ffi) ffi_state_free(it.ffi);
    ModuleEntry *m = it.modules;
    while (m) { ModuleEntry *n = m->next; free(m->path); free(m->alias); free(m); m = n; }
    /* NOTE: for the one-shot path the caller (main.c) owns and frees `program`;
     * we retain no programs here so it->programs stays NULL. */
    free(it.programs);
    return rc;
}
