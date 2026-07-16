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

#include "types.h"

const char *type_name(Type t) {
    switch (t) {
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_INT:     return "int";
        case TYPE_FLOAT:   return "float";
        case TYPE_STR:     return "str";
        case TYPE_CHAR:    return "char";
        case TYPE_BOOL:    return "bool";
        case TYPE_VOID:    return "void";
        case TYPE_ERROR:   return "error";
        case TYPE_NIL:     return "myon.nil";
    }
    return "?";
}

int type_equal(Type a, Type b) {
    return a == b;
}
