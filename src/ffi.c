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
#include <string.h>

/*
 * A loaded-library slot.  Handle IDs are the index into `slots`; a NULL `lib`
 * marks a never-used or already-closed (retired) slot.  This mirrors the
 * ModuleEntry-style bookkeeping used elsewhere in the interpreter but is kept
 * as a growable array for O(1) handle lookup.
 */
/*
 * A raw memory block (Phase3.1).  Block IDs are the index into `blocks`; a
 * NULL `ptr` marks a never-used or already-freed slot.  Mirrors the loaded-
 * library slot bookkeeping above: IDs are handed out in allocation order and
 * never recycled once freed.
 */
typedef struct {
    void      *ptr;   /* NULL == free/retired slot */
    long long  size;  /* allocation size in bytes */
} FFIMemBlock;

struct FFIState {
    FFILib **slots;
    int      count;    /* number of allocated slots (== next handle ID) */
    int      capacity;

    /* Phase3.1 raw memory blocks. */
    FFIMemBlock *blocks;
    int          block_count;    /* == next block ID */
    int          block_capacity;
};

FFIState *ffi_state_create(void) {
    FFIState *st = (FFIState *)myon_xmalloc(sizeof(FFIState));
    st->slots = NULL;
    st->count = 0;
    st->capacity = 0;
    st->blocks = NULL;
    st->block_count = 0;
    st->block_capacity = 0;
    return st;
}

void ffi_state_free(FFIState *st) {
    if (!st) return;
    for (int i = 0; i < st->count; i++) {
        if (st->slots[i]) ffi_platform_close(st->slots[i]);
    }
    free(st->slots);
    /* free any memory blocks the script forgot to release (leak guard) */
    for (int i = 0; i < st->block_count; i++) {
        if (st->blocks[i].ptr) free(st->blocks[i].ptr);
    }
    free(st->blocks);
    free(st);
}

/* ---- Phase3.1 raw memory blocks ---- */

long long ffi_mem_alloc(FFIState *st, long long size) {
    if (size <= 0) return -1;

    void *mem = malloc((size_t)size);
    if (!mem) return -1;
    memset(mem, 0, (size_t)size); /* zero-clear (calloc-equivalent) */

    if (st->block_count >= st->block_capacity) {
        int newcap = st->block_capacity ? st->block_capacity * 2 : 4;
        st->blocks = (FFIMemBlock *)myon_xrealloc(
            st->blocks, sizeof(FFIMemBlock) * (size_t)newcap);
        st->block_capacity = newcap;
    }
    long long id = st->block_count;
    st->blocks[st->block_count].ptr = mem;
    st->blocks[st->block_count].size = size;
    st->block_count++;
    return id;
}

void *ffi_mem_ptr(FFIState *st, long long block_id) {
    if (block_id < 0 || block_id >= st->block_count) return NULL;
    return st->blocks[block_id].ptr; /* NULL if freed/retired */
}

long long ffi_mem_size(FFIState *st, long long block_id) {
    if (block_id < 0 || block_id >= st->block_count) return -1;
    if (!st->blocks[block_id].ptr) return -1; /* freed */
    return st->blocks[block_id].size;
}

int ffi_mem_free(FFIState *st, long long block_id) {
    if (block_id < 0 || block_id >= st->block_count) return 0;
    if (!st->blocks[block_id].ptr) return 0; /* already freed */
    free(st->blocks[block_id].ptr);
    st->blocks[block_id].ptr = NULL; /* retire; never reused */
    st->blocks[block_id].size = 0;
    return 1;
}

int ffi_mem_write(FFIState *st, long long block_id, long long offset,
                  const unsigned char *data, long long len) {
    if (len < 0 || offset < 0) return 0;
    long long size = ffi_mem_size(st, block_id);
    if (size < 0) return 0;                 /* invalid / freed */
    if (offset > size || len > size - offset) return 0; /* out of range */
    void *base = ffi_mem_ptr(st, block_id);
    if (!base) return 0;
    if (len > 0) memcpy((unsigned char *)base + offset, data, (size_t)len);
    return 1;
}

unsigned char *ffi_mem_read(FFIState *st, long long block_id, long long offset,
                            long long len) {
    if (len < 0 || offset < 0) return NULL;
    long long size = ffi_mem_size(st, block_id);
    if (size < 0) return NULL;              /* invalid / freed */
    if (offset > size || len > size - offset) return NULL; /* out of range */
    void *base = ffi_mem_ptr(st, block_id);
    if (!base) return NULL;
    unsigned char *dst = (unsigned char *)myon_xmalloc((size_t)len + 1);
    if (len > 0) memcpy(dst, (unsigned char *)base + offset, (size_t)len);
    dst[len] = '\0'; /* NUL guard so callers may treat it as a C string */
    return dst;
}

int ffi_mem_read_i64(FFIState *st, long long block_id, long long offset,
                     long long *out) {
    if (offset < 0) return 0;
    long long size = ffi_mem_size(st, block_id);
    if (size < 0) return 0;                 /* invalid / freed */
    if (offset > size || (long long)sizeof(long long) > size - offset) return 0;
    const unsigned char *base = (const unsigned char *)ffi_mem_ptr(st, block_id);
    if (!base) return 0;
    /* assemble little-endian (x86-64 native) */
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (unsigned long long)base[offset + i] << (8 * i);
    }
    *out = (long long)v;
    return 1;
}

char *ffi_read_cstring(long long addr, long long max_len) {
    if (addr == 0) return NULL; /* NULL pointer */
    if (max_len <= 0) return NULL;

    const char *src = (const char *)(size_t)addr;
    /* Bounded scan for the NUL terminator (avoids runaway reads). */
    long long n = 0;
    while (n < max_len && src[n] != '\0') n++;

    char *dst = (char *)myon_xmalloc((size_t)n + 1);
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
    return dst;
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
