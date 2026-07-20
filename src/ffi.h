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

/*
 * Phase3.1 — raw memory blocks.
 *
 * These let Myon scripts obtain writable scratch memory to hand to C
 * functions (out-parameters, byte buffers, ...) without ever exposing a raw
 * pointer to the script layer.  Blocks are tracked inside FFIState with the
 * same handle-ID discipline as loaded libraries: alloc hands out a
 * non-negative block ID, and everything else is addressed through that ID.
 * The real address is only materialised internally (via ffi_mem_ptr) when a
 * block is passed to a C call, so an invalid ID can never crash the callee.
 */

/*
 * Allocate `size` zero-initialised bytes and return a non-negative block ID.
 * Returns -1 on failure (size <= 0 or out of memory).
 */
long long ffi_mem_alloc(FFIState *st, long long size);

/*
 * Resolve a block ID to its raw pointer.  Returns NULL for an invalid or
 * already-freed ID.  Internal use only — never exposed to Myon scripts.
 */
void *ffi_mem_ptr(FFIState *st, long long block_id);

/* Size of the block in bytes, or -1 for an invalid/freed ID. */
long long ffi_mem_size(FFIState *st, long long block_id);

/* Free a block.  Returns 1 on success, 0 for an invalid/already-freed ID. */
int ffi_mem_free(FFIState *st, long long block_id);

/*
 * Phase3.1 — read a NUL-terminated C string from a raw address.
 *
 * `addr` is a *raw C address value* (as returned by call_p or another
 * call_*), NOT a block ID — the two are separate namespaces.  Reads at most
 * `max_len` bytes and returns a freshly heap-allocated copy (caller frees).
 * Returns NULL if addr is 0 (NULL).  Passing a bogus non-zero address may
 * crash — this phase does not install signal handlers.
 */
char *ffi_read_cstring(long long addr, long long max_len);

/*
 * Phase3.1 — byte-level read/write on a memory block.
 *
 * These operate on a block ID (from ffi_mem_alloc), NOT a raw address, so an
 * invalid ID or an out-of-range span can never corrupt memory.  A block plus
 * a separately-tracked valid length is Myon's stand-in for a length-prefixed
 * byte string (no dedicated byte-string value type is introduced in this
 * phase).
 */

/*
 * Write `len` bytes from `data` into block `block_id` starting at `offset`.
 * Fails (returns 0) if the ID is invalid/freed or [offset, offset+len) would
 * run past the end of the block.  Returns 1 on success.
 */
int ffi_mem_write(FFIState *st, long long block_id, long long offset,
                  const unsigned char *data, long long len);

/*
 * Read `len` bytes from block `block_id` starting at `offset` into a freshly
 * heap-allocated buffer (caller frees).  Returns NULL for an invalid/freed ID
 * or an out-of-range span.
 */
unsigned char *ffi_mem_read(FFIState *st, long long block_id, long long offset,
                            long long len);

/*
 * Phase3.1 — read 8 bytes of block `block_id` at `offset` as a little-endian
 * int64 (x86-64 native order).  This is the convenience path for pulling an
 * out-parameter pointer (e.g. the `sqlite3*` written by sqlite3_open) back out
 * of a block as a plain address value.  Returns 1 on success and stores the
 * value in *out; returns 0 for an invalid ID or an out-of-range span.
 */
int ffi_mem_read_i64(FFIState *st, long long block_id, long long offset,
                     long long *out);

#endif /* MYON_FFI_H */
