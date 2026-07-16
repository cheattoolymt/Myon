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

#ifndef MYON_TYPES_H
#define MYON_TYPES_H

/*
 * Myon's type system (spec.md section 2).
 * Steps 0-5 use the primitive types plus the special error / nil types.
 * Compound types (array/map/struct) are reserved for later steps.
 */
typedef enum {
    TYPE_UNKNOWN = 0, /* not yet inferred / annotation omitted */
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_CHAR,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_ERROR,       /* the error type; error(...) constructor */
    TYPE_NIL          /* myon.nil : error-context sentinel only (2.4) */
} Type;

const char *type_name(Type t);

/*
 * Type compatibility for binary arithmetic/comparison.
 * The language is strict: operands of different types are an error (2.2).
 */
int type_equal(Type a, Type b);

#endif /* MYON_TYPES_H */
