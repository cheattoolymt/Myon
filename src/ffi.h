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

#ifndef MYON_FFI_H
#define MYON_FFI_H

/*
 * Phase3 C FFI — type / handle management layer.
 *
 * Maps opaque library handle IDs (exposed to Myon as plain int values) to the
 * loaded FFILib* objects from ffi_platform.  One FFIState lives per interpreter
 * instance.  Handle IDs are handed out in load order (0, 1, 2, ...) and are
 * never recycled once closed.
 */

typedef struct FFIState FFIState;

FFIState *ffi_state_create(void);
void      ffi_state_free(FFIState *st);

/*
 * Load a shared library.  On success returns a non-negative handle ID.  On
 * failure returns -1 and stores a heap-allocated error message in *err_msg
 * (the caller must free it).
 */
long long ffi_load(FFIState *st, const char *path, char **err_msg);

/*
 * Resolve a symbol in a previously loaded library.  Returns NULL if the handle
 * is invalid/closed or the symbol is missing.
 */
void *ffi_lookup_symbol(FFIState *st, long long handle,
                        const char *symbol_name);

/* Close a handle.  Returns 1 on success, 0 for an invalid/closed handle. */
int ffi_close(FFIState *st, long long handle);

#endif /* MYON_FFI_H */
