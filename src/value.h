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

#ifndef MYON_VALUE_H
#define MYON_VALUE_H

#include "types.h"

/*
 * Runtime value representation (spec.md section 2).
 * Strings are heap-owned; char is stored as a single-codepoint string
 * for simplicity given Myon source is UTF-8.
 */
typedef struct {
    Type type;
    union {
        long long i;   /* int, char-as-codepoint fallback */
        double    f;   /* float */
        int       b;   /* bool  */
        char     *s;   /* str / char / error message (heap) */
    } as;
} Value;

Value value_int(long long v);
Value value_float(double v);
Value value_bool(int v);
Value value_str(char *heap_str);   /* takes ownership */
Value value_char(char *heap_str);  /* takes ownership; 1 grapheme */
Value value_error(char *heap_msg); /* takes ownership */
Value value_nil(void);
Value value_void(void);

Value value_copy(const Value *v);
void  value_free(Value *v);

/* Human-readable rendering used by myon.print and str(). */
char *value_to_cstr(const Value *v);   /* heap-allocated */
int   value_truthy(const Value *v);

#endif /* MYON_VALUE_H */
