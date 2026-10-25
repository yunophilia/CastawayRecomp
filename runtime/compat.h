#pragma once
// MSVC C runtime functions the runtime uses, for other platforms (force-included there by
// CMakeLists.txt).
#ifndef _WIN32
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

inline int fopen_s(FILE** f, const char* name, const char* mode)
{
    *f = fopen(name, mode);
    return *f ? 0 : errno;
}

inline int strncpy_s(char* dst, size_t dstSize, const char* src, size_t count)
{
    if (!dst || !dstSize) return EINVAL;
    size_t n = strnlen(src, count);
    if (n >= dstSize) n = dstSize - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}

template <typename T, size_t N> constexpr size_t _countof(T (&)[N]) { return N; }

inline int localtime_s(struct tm* out, const time_t* t) { return localtime_r(t, out) ? 0 : errno; }
inline time_t _mkgmtime(struct tm* t) { return timegm(t); }

inline uint32_t _rotl(uint32_t v, int n)
{
    n &= 31;
    return n ? (v << n) | (v >> (32 - n)) : v;
}

#if defined(__x86_64__) || defined(__i386__)
#define YieldProcessor() __builtin_ia32_pause()
#else
#define YieldProcessor() ((void)0)
#endif
#endif
