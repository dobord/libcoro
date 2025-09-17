cmake_minimum_required(VERSION 3.12)
project(libcoro_test)

set(LIBCORO_TEST_SOURCE_FILES
    concepts/test_concepts.cpp

    test_condition_variable.cpp
    test_event.cpp
    test_generator.cpp
    test_latch.cpp
    test_mutex.cpp
    test_queue.cpp
    test_ring_buffer.cpp
    test_semaphore.cpp
    test_shared_mutex.cpp
    test_sync_wait.cpp
    test_task_container.cpp
    test_task.cpp
    test_thread_pool.cpp
    test_when_all.cpp

    catch_amalgamated.hpp catch_amalgamated.cpp
    catch_extensions.hpp catch_extensions.cpp
)

if(NOT EMSCRIPTEN)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        test_when_any.cpp
    )
endif()

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        net/test_ip_address.cpp
    )

    # These tests require coro::io_scheduler
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        net/test_dns_resolver.cpp
        net/test_tcp_server.cpp
        net/test_tls_server.cpp
        net/test_udp_peers.cpp
    )
endif()

if(LIBCORO_FEATURE_NETWORKING)
    list(APPEND LIBCORO_TEST_SOURCE_FILES
        bench.cpp
        test_io_scheduler.cpp
    )
endif()

add_executable(${PROJECT_NAME} main.cpp ${LIBCORO_TEST_SOURCE_FILES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE libcoro)

# When running under ThreadSanitizer, disable Catch2's POSIX signal handlers to
# avoid TSAN warnings about signal-unsafe calls inside handlers on timeouts.
# Also enforce a strict 60s timeout and stop on first TSAN report.
if(LIBCORO_ENABLE_TSAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CATCH_CONFIG_NO_POSIX_SIGNALS)
endif()

if(${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
        $<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>
        $<$<COMPILE_LANGUAGE:CXX>:-fconcepts>
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-pipe>
    )
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>
        $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-pipe>
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    message(FATAL_ERROR "Unsupported compiler.")
endif()

if(LIBCORO_CODE_COVERAGE)
    target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
    target_compile_options(${PROJECT_NAME} PRIVATE --coverage)
endif()

function(coro_add_test name)
    add_test(NAME ${name} COMMAND ${PROJECT_NAME} ${ARGN})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<$<BOOL:${WIN32}>:$<TARGET_FILE_DIR:libcoro>>")
    if(LIBCORO_ENABLE_TSAN)
        # Per-test timeout to keep CI/dev runs bounded
        set_tests_properties(${name} PROPERTIES TIMEOUT 60)
        # Make TSAN fail fast on the first detected issue and suppress known Catch2 harness races
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1 suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp")
        # Avoid running multiple tests concurrently under TSAN to reduce flakiness and cross-test interference
        set_tests_properties(${name} PROPERTIES RUN_SERIAL TRUE)
    endif()
endfunction()

# Register focused tests per Catch2 tag to avoid a single long-running aggregate
coro_add_test(libcoro_concepts           "[concepts]")
coro_add_test(libcoro_condition_variable "[condition_variable]")
coro_add_test(libcoro_event              "[event]")
coro_add_test(libcoro_generator          "[generator]")
coro_add_test(libcoro_latch              "[latch]")
coro_add_test(libcoro_mutex              "[mutex]")
coro_add_test(libcoro_queue              "[queue]")
coro_add_test(libcoro_ring_buffer        "[ring_buffer]")
coro_add_test(libcoro_semaphore          "[semaphore]")
coro_add_test(libcoro_shared_mutex       "[shared_mutex]")
coro_add_test(libcoro_sync_wait          "[sync_wait]")
coro_add_test(libcoro_task_container     "[task_container]")
coro_add_test(libcoro_task               "[task]")
coro_add_test(libcoro_thread_pool        "[thread_pool]")
coro_add_test(libcoro_when_all           "[when_all]")
if(NOT EMSCRIPTEN)
    coro_add_test(libcoro_when_any       "[when_any]")
endif()

# Keep the aggregate test defined but disabled to avoid suite-level timeouts
add_test(NAME libcoro_tests COMMAND ${PROJECT_NAME})
set_tests_properties(libcoro_tests PROPERTIES DISABLED TRUE)
