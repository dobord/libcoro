// Unified stop_token compatibility layer for libcoro.
// Goal: expose real std::stop_token only when the implementation actually provides
// working types (not merely the header stubbed out due to no thread support).
// Detection macro: LIBCORO_HAS_STOP_TOKEN (0/1).
// User can force disable via -DLIBCORO_DISABLE_STOP_TOKEN or force enable by
// defining LIBCORO_HAS_STOP_TOKEN explicitly before including any libcoro header.
//
// Rationale for refined detection:
// On some Android NDK revisions the <stop_token> header is present but compiled
// out when _LIBCPP_HAS_NO_THREADS is defined, yielding missing std::stop_source /
// std::stop_token and breaking a naive __has_include based test. We therefore:
//   1. Require the feature-test macro __cpp_lib_jthread (stop_token/jthread group).
//   2. Require that _LIBCPP_HAS_NO_THREADS is NOT defined (libc++ internal macro).
// If either condition fails we fall back to lightweight stub types.
//
// NOTE: We intentionally include <version> first to surface feature test macros.
// This header is purely a detection helper; it keeps public ABI stable by only
// exposing minimal stub functionality when real cancellation is unavailable.

#pragma once

#if defined(LIBCORO_DISABLE_STOP_TOKEN)
    #undef LIBCORO_HAS_STOP_TOKEN
    #define LIBCORO_HAS_STOP_TOKEN 0
#elif defined(LIBCORO_HAS_STOP_TOKEN)
    // User override respected (must be 0 or 1). Nothing further.
#else
    #if __has_include(<stop_token>)
        #if __has_include(<version>)
            #include <version>
        #endif
        #include <stop_token>
        #if defined(__cpp_lib_jthread) && !defined(_LIBCPP_HAS_NO_THREADS)
            #define LIBCORO_HAS_STOP_TOKEN 1
        #else
            #define LIBCORO_HAS_STOP_TOKEN 0
        #endif
    #else
        #define LIBCORO_HAS_STOP_TOKEN 0
    #endif
#endif

#if !LIBCORO_HAS_STOP_TOKEN
namespace std {
    struct stop_token {
        bool stop_requested() const noexcept { return false; }
    };
    struct stop_source {
        stop_token get_token() const noexcept { return {}; }
        void request_stop() const noexcept {}
    };
}
#endif
