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

#include "ast.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Expression constructors                                             */
/* ------------------------------------------------------------------ */

static Expr *expr_alloc(ExprKind kind, int line) {
    Expr *e = (Expr *)myon_xmalloc(sizeof(Expr));
    memset(e, 0, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

Expr *expr_int(long long v, int line) {
    Expr *e = expr_alloc(EXPR_INT_LIT, line);
    e->as.int_val = v;
    return e;
}

Expr *expr_float(double v, int line) {
    Expr *e = expr_alloc(EXPR_FLOAT_LIT, line);
    e->as.float_val = v;
    return e;
}

Expr *expr_bool(int v, int line) {
    Expr *e = expr_alloc(EXPR_BOOL_LIT, line);
    e->as.bool_val = v;
    return e;
}

Expr *expr_string(char *raw, int line) {
    Expr *e = expr_alloc(EXPR_STRING, line);
    e->as.str_val = raw;
    return e;
}

Expr *expr_nil(int line) {
    return expr_alloc(EXPR_NIL, line);
}

Expr *expr_ident(char *name, int line) {
    Expr *e = expr_alloc(EXPR_IDENT, line);
    e->as.ident = name;
    return e;
}

Expr *expr_ctor(ExprKind kind, Expr *operand, int line) {
    Expr *e = expr_alloc(kind, line);
    e->as.operand = operand;
    return e;
}

Expr *expr_binary(OpKind op, Expr *l, Expr *r, int line) {
    Expr *e = expr_alloc(EXPR_BINARY, line);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

Expr *expr_logical(OpKind op, Expr *l, Expr *r, int line) {
    Expr *e = expr_alloc(EXPR_LOGICAL, line);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    return e;
}

Expr *expr_unary(OpKind op, Expr *operand, int line) {
    Expr *e = expr_alloc(EXPR_UNARY, line);
    e->as.unary.op = op;
    e->as.unary.operand = operand;
    return e;
}

Expr *expr_call(Expr *callee, int line) {
    Expr *e = expr_alloc(EXPR_CALL, line);
    e->as.call.callee = callee;
    e->as.call.args = NULL;
    e->as.call.arg_names = NULL;
    e->as.call.arg_count = 0;
    return e;
}

Expr *expr_index(Expr *target, Expr *index, int line) {
    Expr *e = expr_alloc(EXPR_INDEX, line);
    e->as.index.target = target;
    e->as.index.index = index;
    return e;
}

Expr *expr_member(Expr *target, char *name, int line) {
    Expr *e = expr_alloc(EXPR_MEMBER, line);
    e->as.member.target = target;
    e->as.member.name = name;
    return e;
}

void call_add_arg(CallExpr *call, char *name, Expr *arg) {
    int n = call->arg_count + 1;
    call->args = (Expr **)myon_xrealloc(call->args, sizeof(Expr *) * n);
    call->arg_names = (char **)myon_xrealloc(call->arg_names, sizeof(char *) * n);
    call->args[call->arg_count] = arg;
    call->arg_names[call->arg_count] = name;
    call->arg_count = n;
}

/* ------------------------------------------------------------------ */
/* Statement constructors / lists                                      */
/* ------------------------------------------------------------------ */

Stmt *stmt_new(StmtKind kind, int line) {
    Stmt *s = (Stmt *)myon_xmalloc(sizeof(Stmt));
    memset(s, 0, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

void stmtlist_init(StmtList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void stmtlist_push(StmtList *list, Stmt *s) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 8;
        list->items = (Stmt **)myon_xrealloc(list->items,
                                             sizeof(Stmt *) * list->capacity);
    }
    list->items[list->count++] = s;
}

/* ------------------------------------------------------------------ */
/* Destructors                                                         */
/* ------------------------------------------------------------------ */

void expr_free(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_STRING:    free(e->as.str_val); break;
        case EXPR_IDENT:     free(e->as.ident);   break;
        case EXPR_STR_CTOR:
        case EXPR_CHAR_CTOR:
        case EXPR_INT_CTOR:
        case EXPR_ERROR_CTOR: expr_free(e->as.operand); break;
        case EXPR_BINARY:
        case EXPR_LOGICAL:
            expr_free(e->as.binary.left);
            expr_free(e->as.binary.right);
            break;
        case EXPR_UNARY:
            expr_free(e->as.unary.operand);
            break;
        case EXPR_CALL:
            expr_free(e->as.call.callee);
            for (int i = 0; i < e->as.call.arg_count; i++) {
                expr_free(e->as.call.args[i]);
                free(e->as.call.arg_names[i]);
            }
            free(e->as.call.args);
            free(e->as.call.arg_names);
            break;
        case EXPR_INDEX:
            expr_free(e->as.index.target);
            expr_free(e->as.index.index);
            break;
        case EXPR_MEMBER:
            expr_free(e->as.member.target);
            free(e->as.member.name);
            break;
        default: break;
    }
    free(e);
}

void stmt_free(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_MODULE:
            free(s->as.module_decl.path);
            free(s->as.module_decl.alias);
            break;
        case STMT_ASSIGN:
            free(s->as.assign.name);
            expr_free(s->as.assign.value);
            for (int i = 0; i < s->as.assign.extra_count; i++)
                free(s->as.assign.extra_names[i]);
            free(s->as.assign.extra_names);
            break;
        case STMT_EXPR:
            expr_free(s->as.expr);
            break;
        case STMT_IF:
            expr_free(s->as.if_stmt.cond);
            stmtlist_free(&s->as.if_stmt.then_body);
            for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
                expr_free(s->as.if_stmt.elifs[i].cond);
                stmtlist_free(&s->as.if_stmt.elifs[i].body);
            }
            free(s->as.if_stmt.elifs);
            if (s->as.if_stmt.has_else)
                stmtlist_free(&s->as.if_stmt.else_body);
            break;
        case STMT_WHILE:
            expr_free(s->as.while_stmt.cond);
            stmtlist_free(&s->as.while_stmt.body);
            break;
        case STMT_FOR:
            free(s->as.for_stmt.var);
            expr_free(s->as.for_stmt.range_start);
            expr_free(s->as.for_stmt.range_end);
            expr_free(s->as.for_stmt.iterable);
            stmtlist_free(&s->as.for_stmt.body);
            break;
        case STMT_BLOCK:
            stmtlist_free(&s->as.block);
            break;
        case STMT_EXPOSE:
            free(s->as.expose_name);
            break;
        default: break;
    }
    free(s);
}

void stmtlist_free(StmtList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++)
        stmt_free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

void program_free(Program *p) {
    if (!p) return;
    stmtlist_free(&p->stmts);
}
