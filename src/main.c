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
        "Myon interpreter (Steps 0-5)\n"
        "usage: %s [--tokens] <file.myon>\n"
        "       %s --tokens -   (read source from stdin)\n"
        "  --tokens   print the token stream and exit (Step 1 check)\n",
        prog, prog);
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

    TokenList tokens;
    if (!lexer_tokenize(source, &tokens)) {
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
    free(source);
    return rc;
}
