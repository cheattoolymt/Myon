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

#include "ffi.h"
#include "ffi_platform.h"
#include "common.h"

#include <stddef.h>

/*
 * A loaded-library slot.  Handle IDs are the index into `slots`; a NULL `lib`
 * marks a never-used or already-closed (retired) slot.  This mirrors the
 * ModuleEntry-style bookkeeping used elsewhere in the interpreter but is kept
 * as a growable array for O(1) handle lookup.
 */
struct FFIState {
    FFILib **slots;
    int      count;    /* number of allocated slots (== next handle ID) */
    int      capacity;
};

FFIState *ffi_state_create(void) {
    FFIState *st = (FFIState *)myon_xmalloc(sizeof(FFIState));
    st->slots = NULL;
    st->count = 0;
    st->capacity = 0;
    return st;
}

void ffi_state_free(FFIState *st) {
    if (!st) return;
    for (int i = 0; i < st->count; i++) {
        if (st->slots[i]) ffi_platform_close(st->slots[i]);
    }
    free(st->slots);
    free(st);
}

long long ffi_load(FFIState *st, const char *path, char **err_msg) {
    FFILib *lib = ffi_platform_load(path, err_msg);
    if (!lib) return -1;

    if (st->count >= st->capacity) {
        int newcap = st->capacity ? st->capacity * 2 : 4;
        st->slots = (FFILib **)myon_xrealloc(st->slots,
                                             sizeof(FFILib *) * (size_t)newcap);
        st->capacity = newcap;
    }
    long long id = st->count;
    st->slots[st->count++] = lib;
    return id;
}

void *ffi_lookup_symbol(FFIState *st, long long handle,
                        const char *symbol_name) {
    if (handle < 0 || handle >= st->count) return NULL;
    FFILib *lib = st->slots[handle];
    if (!lib) return NULL; /* closed / retired */
    return ffi_platform_sym(lib, symbol_name);
}

int ffi_close(FFIState *st, long long handle) {
    if (handle < 0 || handle >= st->count) return 0;
    FFILib *lib = st->slots[handle];
    if (!lib) return 0; /* already closed */
    ffi_platform_close(lib);
    st->slots[handle] = NULL; /* retire; never reused */
    return 1;
}
