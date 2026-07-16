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

#ifndef MYON_INTERPRETER_H
#define MYON_INTERPRETER_H

#include "ast.h"

/*
 * Tree-walking interpreter.
 * Implements Steps 3-5: assignments, myon.print, arithmetic with strict
 * type checking (spec 2.2), casts (2.3), and control flow (section 5).
 *
 * Returns 0 on success, non-zero on a runtime error (message printed).
 */
int interpret(Program *program);

#endif /* MYON_INTERPRETER_H */
