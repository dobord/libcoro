#pragma once

// Lightweight wrappers for ThreadSanitizer annotations so TSAN can recognize
// our custom synchronization primitives and coroutine hand-offs.
// They compile to no-ops when TSAN is not enabled.

namespace coro::detail
{

#if defined(__has_feature)
    #if __has_feature(thread_sanitizer)
        #define CORO_TSAN_ENABLED 1
    #endif
#endif

#if !defined(CORO_TSAN_ENABLED)
    #if defined(__SANITIZE_THREAD__)
        #define CORO_TSAN_ENABLED 1
    #endif
#endif

#if CORO_TSAN_ENABLED
extern "C"
{
    void __tsan_acquire(void* addr);
    void __tsan_release(void* addr);
}

inline void tsan_acquire(const void* addr)
{
    __tsan_acquire(const_cast<void*>(addr));
}
inline void tsan_release(const void* addr)
{
    __tsan_release(const_cast<void*>(addr));
}

#else

inline void tsan_acquire(const void*)
{
}
inline void tsan_release(const void*)
{
}

#endif

} // namespace coro::detail
