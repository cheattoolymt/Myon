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

#include "value.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value value_int(long long v)   { Value x; x.type = TYPE_INT;   x.as.i = v; return x; }
Value value_float(double v)    { Value x; x.type = TYPE_FLOAT; x.as.f = v; return x; }
Value value_bool(int v)        { Value x; x.type = TYPE_BOOL;  x.as.b = v ? 1 : 0; return x; }
Value value_str(char *s)       { Value x; x.type = TYPE_STR;   x.as.s = s; return x; }
Value value_char(char *s)      { Value x; x.type = TYPE_CHAR;  x.as.s = s; return x; }
Value value_error(char *m)     { Value x; x.type = TYPE_ERROR; x.as.s = m; return x; }
Value value_nil(void)          { Value x; x.type = TYPE_NIL;   x.as.s = NULL; return x; }
Value value_void(void)         { Value x; x.type = TYPE_VOID;  x.as.i = 0; return x; }

static int is_heap_string(const Value *v) {
    return v->type == TYPE_STR || v->type == TYPE_CHAR || v->type == TYPE_ERROR;
}

Value value_copy(const Value *v) {
    Value out = *v;
    if (is_heap_string(v) && v->as.s)
        out.as.s = myon_strdup(v->as.s);
    return out;
}

void value_free(Value *v) {
    if (!v) return;
    if (is_heap_string(v) && v->as.s) {
        free(v->as.s);
        v->as.s = NULL;
    }
}

char *value_to_cstr(const Value *v) {
    char buf[64];
    switch (v->type) {
        case TYPE_INT:
            snprintf(buf, sizeof(buf), "%lld", v->as.i);
            return myon_strdup(buf);
        case TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%g", v->as.f);
            return myon_strdup(buf);
        case TYPE_BOOL:
            return myon_strdup(v->as.b ? "true" : "false");
        case TYPE_STR:
        case TYPE_CHAR:
            return myon_strdup(v->as.s ? v->as.s : "");
        case TYPE_ERROR:
            return myon_strdup(v->as.s ? v->as.s : "error");
        case TYPE_NIL:
            return myon_strdup("myon.nil");
        case TYPE_VOID:
            return myon_strdup("void");
        default:
            return myon_strdup("");
    }
}

int value_truthy(const Value *v) {
    switch (v->type) {
        case TYPE_BOOL:  return v->as.b;
        case TYPE_INT:   return v->as.i != 0;
        case TYPE_FLOAT: return v->as.f != 0.0;
        case TYPE_NIL:   return 0;
        default:         return 1;
    }
}
