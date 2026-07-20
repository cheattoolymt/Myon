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

#include "ffi_platform.h"

#include <stdlib.h>
#include <string.h>

/* Duplicate a C string onto the heap (self-contained so this file has no
 * dependency on the rest of the interpreter). */
static char *ffi_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

#if defined(__linux__)

#include <dlfcn.h>

struct FFILib {
    void *handle; /* dlopen handle */
};

FFILib *ffi_platform_load(const char *path, char **err_msg) {
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char *e = dlerror();
        if (err_msg) *err_msg = ffi_dup(e ? e : "dlopen failed");
        return NULL;
    }
    FFILib *lib = (FFILib *)malloc(sizeof(FFILib));
    if (!lib) {
        dlclose(h);
        if (err_msg) *err_msg = ffi_dup("out of memory loading library");
        return NULL;
    }
    lib->handle = h;
    return lib;
}

void *ffi_platform_sym(FFILib *lib, const char *name) {
    if (!lib || !lib->handle) return NULL;
    /* Clear any stale error, then resolve. */
    dlerror();
    return dlsym(lib->handle, name);
}

void ffi_platform_close(FFILib *lib) {
    if (!lib) return;
    if (lib->handle) dlclose(lib->handle);
    free(lib);
}

int ffi_platform_supported(void) {
    return 1;
}

#elif defined(__APPLE__)

/* Phase3 stub.  macOS ships the same dlopen family, so a real implementation
 * can be added here later; for now FFI is intentionally unavailable. */
struct FFILib { int unused; };

FFILib *ffi_platform_load(const char *path, char **err_msg) {
    (void)path;
    if (err_msg) *err_msg = ffi_dup("FFI is not supported on macOS yet (Phase3 stub)");
    return NULL;
}

void *ffi_platform_sym(FFILib *lib, const char *name) {
    (void)lib; (void)name;
    return NULL;
}

void ffi_platform_close(FFILib *lib) {
    (void)lib;
}

int ffi_platform_supported(void) {
    return 0;
}

#elif defined(_WIN32)

/* Phase3 stub.  A real implementation would use LoadLibraryA /
 * GetProcAddress / FreeLibrary. */
struct FFILib { int unused; };

FFILib *ffi_platform_load(const char *path, char **err_msg) {
    (void)path;
    if (err_msg) *err_msg = ffi_dup("FFI is not supported on Windows yet (Phase3 stub)");
    return NULL;
}

void *ffi_platform_sym(FFILib *lib, const char *name) {
    (void)lib; (void)name;
    return NULL;
}

void ffi_platform_close(FFILib *lib) {
    (void)lib;
}

int ffi_platform_supported(void) {
    return 0;
}

#else

/* Unknown platform: behave like the macOS/Windows stubs. */
struct FFILib { int unused; };

FFILib *ffi_platform_load(const char *path, char **err_msg) {
    (void)path;
    if (err_msg) *err_msg = ffi_dup("FFI is not supported on this platform");
    return NULL;
}

void *ffi_platform_sym(FFILib *lib, const char *name) {
    (void)lib; (void)name;
    return NULL;
}

void ffi_platform_close(FFILib *lib) {
    (void)lib;
}

int ffi_platform_supported(void) {
    return 0;
}

#endif
