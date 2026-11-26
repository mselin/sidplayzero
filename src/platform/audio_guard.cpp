// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/audio_guard.h"

#ifndef SPZ_AUDIO_GUARD
#define SPZ_AUDIO_GUARD 0
#endif

#if SPZ_AUDIO_GUARD
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#endif

namespace spz::platform {

#if SPZ_AUDIO_GUARD

namespace {

thread_local int g_inside = 0;

[[noreturn]] void caught(const char* what) {
    g_inside = 0;
    static const char kStart[] = "SPEC-022: the audio callback ";
    (void)!::write(STDOUT_FILENO, kStart, sizeof kStart - 1);
    (void)!::write(STDOUT_FILENO, what, std::strlen(what));
    (void)!::write(STDOUT_FILENO, "\n", 1);
    std::abort();
}

}  // namespace

AudioCallbackScope::AudioCallbackScope() { ++g_inside; }
AudioCallbackScope::~AudioCallbackScope() { --g_inside; }
bool audio_guard_built() { return true; }

}  // namespace spz::platform

// The watch. These definitions in the program come before the C library's: each checks, then hands on
// to the library's own.
extern "C" {

void* __libc_malloc(std::size_t);
void* __libc_calloc(std::size_t, std::size_t);
void* __libc_realloc(void*, std::size_t);
void __libc_free(void*);

void* malloc(std::size_t size) {
    if (spz::platform::g_inside) spz::platform::caught("allocated memory (malloc)");
    return __libc_malloc(size);
}

void* calloc(std::size_t count, std::size_t size) {
    if (spz::platform::g_inside) spz::platform::caught("allocated memory (calloc)");
    return __libc_calloc(count, size);
}

void* realloc(void* old, std::size_t size) {
    if (spz::platform::g_inside) spz::platform::caught("allocated memory (realloc)");
    return __libc_realloc(old, size);
}

void free(void* pointer) { __libc_free(pointer); }

namespace {
using LockFn = int (*)(pthread_mutex_t*);
LockFn real_lock() {
    static LockFn found = reinterpret_cast<LockFn>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
    return found;
}
LockFn real_trylock() {
    static LockFn found = reinterpret_cast<LockFn>(dlsym(RTLD_NEXT, "pthread_mutex_trylock"));
    return found;
}
// Found before main(), while one thread runs, so that no lock ever waits for the lookup.
[[maybe_unused]] const bool g_found = real_lock() != nullptr && real_trylock() != nullptr;
}  // namespace

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (spz::platform::g_inside) spz::platform::caught("took a lock (pthread_mutex_lock)");
    return real_lock()(mutex);
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    if (spz::platform::g_inside) spz::platform::caught("took a lock (pthread_mutex_trylock)");
    return real_trylock()(mutex);
}

}  // extern "C"

#else  // no guard

namespace spz::platform {

AudioCallbackScope::AudioCallbackScope() {}
AudioCallbackScope::~AudioCallbackScope() {}
bool audio_guard_built() { return false; }

}  // namespace spz::platform

#endif
