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

#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include "common.h"
#include "diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "myon: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)myon_xmalloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void dump_tokens(const TokenList *tl) {
    for (int i = 0; i < tl->count; i++) {
        const Token *t = &tl->items[i];
        printf("%3d  %-14s", i, token_type_name(t->type));
        if (t->lexeme && t->type != TOK_NEWLINE)
            printf("  '%s'", t->lexeme);
        printf("  (line %d)\n", t->line);
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Myon interpreter\n"
        "usage: %s [--tokens] <file.myon>\n"
        "       %s --tokens -   (read source from stdin)\n"
        "       %s              (no argument: start interactive REPL)\n"
        "  --tokens   print the token stream and exit (Step 1 check)\n",
        prog, prog, prog);
}

/* ------------------------------------------------------------------ */
/* Interactive REPL (spec 12)                                          */
/* ------------------------------------------------------------------ */

/*
 * Decide whether the accumulated REPL buffer is a syntactically *complete*
 * unit of input.  We use a lightweight scan that mirrors the lexer's own
 * string/comment skipping so that brackets inside string literals or comments
 * are not miscounted.  Input is considered incomplete while any of
 * ()/[]/{} remain open.  This is intentionally conservative: a genuinely
 * malformed line still gets handed to the parser, which reports the error.
 */
static int repl_input_complete(const char *src) {
    int depth = 0;      /* () [] {} nesting */
    for (const char *c = src; *c; c++) {
        if (*c == '"') {
            /* skip string literal, honoring backslash escapes */
            c++;
            while (*c && *c != '"') {
                if (*c == '\\' && c[1]) c++;
                c++;
            }
            if (!*c) return 0; /* unterminated string => keep reading */
            continue;
        }
        if (*c == '#') { /* '#' line comment */
            while (*c && *c != '\n') c++;
            if (!*c) break;
            continue;
        }
        if (*c == '/' && c[1] == '/') {
            while (*c && *c != '\n') c++;
            if (!*c) break;
            continue;
        }
        if (*c == '/' && c[1] == '*') {
            c += 2;
            while (*c && !(*c == '*' && c[1] == '/')) c++;
            if (!*c) return 0; /* unterminated block comment */
            c++; /* land on '/' (loop ++ consumes it) */
            continue;
        }
        if (*c == '(' || *c == '[' || *c == '{') depth++;
        else if (*c == ')' || *c == ']' || *c == '}') { if (depth > 0) depth--; }
    }
    return depth <= 0;
}

static int run_repl(void) {
    Interp *it = interp_create();
    printf("Myon REPL. Type 'exit' or 'quit' to leave (Ctrl+D also works).\n");

    size_t cap = 256, len = 0;
    char *buf = (char *)myon_xmalloc(cap);
    buf[0] = '\0';
    int continuing = 0;

    for (;;) {
        fputs(continuing ? "...> " : "myon> ", stdout);
        fflush(stdout);

        /* read a single physical line */
        char line[1024];
        if (!fgets(line, sizeof(line), stdin)) {
            fputc('\n', stdout);
            break; /* EOF (Ctrl+D) */
        }

        /* exit/quit only when not mid-continuation and buffer is empty */
        if (!continuing) {
            /* trim trailing newline for the command check */
            char trimmed[1024];
            size_t n = 0;
            for (const char *c = line; *c && *c != '\n' && n + 1 < sizeof(trimmed); c++)
                trimmed[n++] = *c;
            trimmed[n] = '\0';
            /* strip surrounding whitespace */
            char *s = trimmed;
            while (*s == ' ' || *s == '\t') s++;
            char *e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
            if (strcmp(s, "exit") == 0 || strcmp(s, "quit") == 0) break;
            if (*s == '\0') continue; /* blank line at top level: ignore */
        }

        /* append the physical line to the buffer */
        size_t ll = strlen(line);
        if (len + ll + 1 > cap) {
            while (len + ll + 1 > cap) cap *= 2;
            buf = (char *)myon_xrealloc(buf, cap);
        }
        memcpy(buf + len, line, ll + 1);
        len += ll;

        /* if input still has open brackets, prompt for continuation */
        if (!repl_input_complete(buf)) {
            continuing = 1;
            continue;
        }

        /* complete unit: tokenize + parse + run */
        TokenList tokens;
        /* P5: register the current input so diagnostics can show a snippet. */
        diag_set_source(buf);
        if (lexer_tokenize(buf, &tokens)) {
            Program *program = parser_parse(&tokens);
            if (program) {
                /* interp_run installs its own setjmp barrier: a runtime error
                 * aborts only this input, the REPL keeps going. */
                interp_run(it, program);
            }
            /* tokens are referenced by retained AST lexemes? No: the parser
             * copies what it needs into the AST, so freeing tokens is safe. */
            token_list_free(&tokens);
        }
        diag_clear_source();

        /* reset the buffer for the next statement */
        len = 0;
        buf[0] = '\0';
        continuing = 0;
    }

    free(buf);
    interp_free(it);
    return 0;
}

int main(int argc, char **argv) {
    int tokens_only = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) tokens_only = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        } else {
            path = argv[i];
        }
    }

    /* No file argument and not a token dump: start the interactive REPL. */
    if (!path && !tokens_only) {
        return run_repl();
    }

    if (!path) { usage(argv[0]); return 64; }

    char *source = NULL;
    if (strcmp(path, "-") == 0) {
        /* read stdin */
        size_t cap = 4096, len = 0;
        source = (char *)myon_xmalloc(cap);
        int c;
        while ((c = fgetc(stdin)) != EOF) {
            if (len + 1 >= cap) { cap *= 2; source = (char *)myon_xrealloc(source, cap); }
            source[len++] = (char)c;
        }
        source[len] = '\0';
    } else {
        source = read_file(path);
        if (!source) return 66;
    }

    /* P5: register the source so lexer/parser/interpreter diagnostics can
     * print the offending line with a caret. */
    diag_set_source(source);

    TokenList tokens;
    if (!lexer_tokenize(source, &tokens)) {
        diag_clear_source();
        free(source);
        return 65;
    }

    if (tokens_only) {
        dump_tokens(&tokens);
        token_list_free(&tokens);
        free(source);
        return 0;
    }

    Program *program = parser_parse(&tokens);
    if (!program) {
        token_list_free(&tokens);
        free(source);
        return 65;
    }

    int rc = interpret(program);

    program_free(program);
    token_list_free(&tokens);
    diag_clear_source();
    free(source);
    return rc;
}
