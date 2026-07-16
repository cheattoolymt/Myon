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

#ifndef MYON_AST_H
#define MYON_AST_H

/*
 * Abstract syntax tree node definitions.
 * Reference: spec.md section 13 (EBNF).
 *
 * The node set is laid out to mirror the grammar so future steps
 * (functions, structs, arrays, etc.) can be filled in incrementally.
 * Steps 0-5 exercise expressions, assignments, print, and control flow.
 */

#include "types.h"

typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    EXPR_INT_LIT,
    EXPR_FLOAT_LIT,
    EXPR_BOOL_LIT,
    EXPR_STR_CTOR,     /* str(expr)   */
    EXPR_CHAR_CTOR,    /* char(expr)  */
    EXPR_INT_CTOR,     /* int(expr)   */
    EXPR_ERROR_CTOR,   /* error(expr) */
    EXPR_STRING,       /* raw "..." string literal (interpolation is Step 10) */
    EXPR_NIL,          /* myon.nil    */
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_LOGICAL,      /* myon.and / myon.or */
    EXPR_CALL,
    EXPR_INDEX,        /* a[b]        */
    EXPR_MEMBER        /* a.b         */
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,
    OP_NEG, OP_NOT
} OpKind;

typedef struct {
    Expr *callee;
    Expr **args;      /* positional arg expressions */
    char **arg_names; /* parallel to args; NULL entry => positional */
    int    arg_count;
} CallExpr;

struct Expr {
    ExprKind kind;
    int      line;
    union {
        long long   int_val;     /* EXPR_INT_LIT  */
        double      float_val;   /* EXPR_FLOAT_LIT */
        int         bool_val;    /* EXPR_BOOL_LIT */
        char       *str_val;     /* EXPR_STRING (raw body)  */
        char       *ident;       /* EXPR_IDENT */
        Expr       *operand;     /* EXPR_*_CTOR / EXPR_UNARY */
        struct { OpKind op; Expr *left; Expr *right; } binary; /* BINARY/LOGICAL */
        struct { OpKind op; Expr *operand; } unary;
        CallExpr    call;
        struct { Expr *target; Expr *index; } index;
        struct { Expr *target; char *name; } member;
    } as;
};

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    STMT_SYSTEM,
    STMT_MODULE,
    STMT_ASSIGN,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_BLOCK,
    STMT_EXPOSE
} StmtKind;

/* A block is a dynamic list of statements. */
typedef struct {
    Stmt **items;
    int    count;
    int    capacity;
} StmtList;

/* One "myon.elif expr then { ... }" arm. */
typedef struct {
    Expr     *cond;
    StmtList  body;
} ElifArm;

struct Stmt {
    StmtKind kind;
    int      line;
    union {
        struct { int version; } system_decl;              /* STMT_SYSTEM */
        struct { char *path; char *alias; } module_decl;   /* STMT_MODULE; alias may be NULL */

        struct {
            char    *name;
            Type     annotated;   /* declared type, or TYPE_UNKNOWN if omitted */
            int      has_type;     /* 1 if an explicit ": type" was given */
            OpKind   compound;     /* OP_ADD/SUB/MUL/DIV for +=,... else -1 */
            int      is_compound;
            Expr    *value;
            /* multi-target assignment: `a, b = f()` (Step 6 groundwork) */
            char   **extra_names;  /* additional LHS names beyond `name` */
            int      extra_count;
        } assign;                                          /* STMT_ASSIGN */

        Expr *expr;                                        /* STMT_EXPR */

        struct {
            Expr     *cond;
            StmtList  then_body;
            ElifArm  *elifs;
            int       elif_count;
            int       has_else;
            StmtList  else_body;
        } if_stmt;                                         /* STMT_IF */

        struct { Expr *cond; StmtList body; } while_stmt;  /* STMT_WHILE */

        struct {
            char    *var;
            int      is_range;
            Expr    *range_start;  /* if is_range */
            Expr    *range_end;    /* if is_range */
            Expr    *iterable;     /* if !is_range */
            StmtList body;
        } for_stmt;                                        /* STMT_FOR */

        StmtList block;                                    /* STMT_BLOCK */
        char    *expose_name;                              /* STMT_EXPOSE */
    } as;
};

/* Whole program. */
typedef struct {
    StmtList stmts;
} Program;

/* ---- constructors ---- */
Expr *expr_int(long long v, int line);
Expr *expr_float(double v, int line);
Expr *expr_bool(int v, int line);
Expr *expr_string(char *raw, int line);
Expr *expr_nil(int line);
Expr *expr_ident(char *name, int line);
Expr *expr_ctor(ExprKind kind, Expr *operand, int line);
Expr *expr_binary(OpKind op, Expr *l, Expr *r, int line);
Expr *expr_logical(OpKind op, Expr *l, Expr *r, int line);
Expr *expr_unary(OpKind op, Expr *operand, int line);
Expr *expr_call(Expr *callee, int line);
Expr *expr_index(Expr *target, Expr *index, int line);
Expr *expr_member(Expr *target, char *name, int line);
void  call_add_arg(CallExpr *call, char *name, Expr *arg);

Stmt *stmt_new(StmtKind kind, int line);
void  stmtlist_init(StmtList *list);
void  stmtlist_push(StmtList *list, Stmt *s);
void  stmtlist_free(StmtList *list);

void  expr_free(Expr *e);
void  stmt_free(Stmt *s);
void  program_free(Program *p);

#endif /* MYON_AST_H */
