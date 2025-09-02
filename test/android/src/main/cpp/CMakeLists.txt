cmake_minimum_required(VERSION 3.18)
project(libcoroTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# JNI not required directly here: JNIEXPORT functions are declared manually.

# --- Paths ---
# PROJECT_SOURCE_DIR = test/android/src/main/cpp
set(ANDROID_APP_ROOT "${PROJECT_SOURCE_DIR}/../../..")            # -> test/android
# From test/android go up two levels (android -> test -> repo root)
get_filename_component(REPO_ROOT "${ANDROID_APP_ROOT}/../.." REALPATH)
set(LIBCORO_DIR "${REPO_ROOT}")
message(STATUS "[android] ANDROID_APP_ROOT=${ANDROID_APP_ROOT}")
message(STATUS "[android] REPO_ROOT=${REPO_ROOT}")
message(STATUS "[android] LIBCORO_DIR=${LIBCORO_DIR}")

# --- OpenSSL prebuilts integration ---
# build_openssl.sh installs into test/android/external/openssl/<ABI>
if(DEFINED ANDROID_ABI)
	set(_OPENSSL_CANDIDATE "${ANDROID_APP_ROOT}/external/openssl/${ANDROID_ABI}")
	if(EXISTS "${_OPENSSL_CANDIDATE}/lib/libssl.a")
	message(STATUS "Using prebuilt OpenSSL in ${_OPENSSL_CANDIDATE}")
		set(OPENSSL_ROOT_DIR "${_OPENSSL_CANDIDATE}" CACHE PATH "")
		set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "")
		set(OPENSSL_INCLUDE_DIR "${_OPENSSL_CANDIDATE}/include" CACHE PATH "")
		set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_CANDIDATE}/lib/libcrypto.a" CACHE FILEPATH "")
		set(OPENSSL_SSL_LIBRARY "${_OPENSSL_CANDIDATE}/lib/libssl.a" CACHE FILEPATH "")
		set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}" CACHE STRING "")
		list(PREPEND CMAKE_PREFIX_PATH "${_OPENSSL_CANDIDATE}")
	else()
		message(WARNING "Prebuilt OpenSSL for ABI ${ANDROID_ABI} not found at ${_OPENSSL_CANDIDATE}. Run build_openssl.sh first.")
	endif()
endif()

# Validate libcoro root
if(NOT EXISTS ${LIBCORO_DIR}/CMakeLists.txt)
	message(FATAL_ERROR "libcoro root CMakeLists.txt not found at ${LIBCORO_DIR}.")
endif()

add_library(coroTest SHARED main.cpp)
# Trim libcoro build to essentials for library build; we'll link test objects manually below.
set(LIBCORO_BUILD_TESTS OFF CACHE BOOL "")
set(LIBCORO_BUILD_EXAMPLES OFF CACHE BOOL "")
add_subdirectory(${LIBCORO_DIR} external_libcoro)
find_library(ANDROID_LOG_LIB log)

# Collect libcoro test sources (without their own main.cpp which defines Catch2 main)
set(LIBCORO_TEST_DIR ${LIBCORO_DIR}/test)
message(STATUS "[android] LIBCORO_TEST_DIR=${LIBCORO_TEST_DIR}")
# Minimal subset of tests matching CI filter tags: event,generator,latch,mutex,queue,semaphore
set(LIBCORO_TEST_SOURCES
	${LIBCORO_TEST_DIR}/test_event.cpp
	${LIBCORO_TEST_DIR}/test_generator.cpp
	${LIBCORO_TEST_DIR}/test_latch.cpp
	${LIBCORO_TEST_DIR}/test_mutex.cpp
	${LIBCORO_TEST_DIR}/test_queue.cpp
	${LIBCORO_TEST_DIR}/test_semaphore.cpp
)

if(LIBCORO_FEATURE_NETWORKING)
	list(APPEND LIBCORO_TEST_SOURCES
		${LIBCORO_TEST_DIR}/net/test_ip_address.cpp
		${LIBCORO_TEST_DIR}/net/test_dns_resolver.cpp
		${LIBCORO_TEST_DIR}/net/test_tcp_server.cpp
		${LIBCORO_TEST_DIR}/net/test_tls_server.cpp
		${LIBCORO_TEST_DIR}/net/test_udp_peers.cpp
		${LIBCORO_TEST_DIR}/test_io_scheduler.cpp
		${LIBCORO_TEST_DIR}/bench.cpp
	)
	list(APPEND LIBCORO_TEST_SOURCES ${LIBCORO_TEST_DIR}/test_when_any.cpp)
endif()

list(TRANSFORM LIBCORO_TEST_SOURCES PREPEND "") # no-op to ensure list exists
target_sources(coroTest PRIVATE ${LIBCORO_TEST_SOURCES}
	${LIBCORO_TEST_DIR}/catch_amalgamated.cpp
	${LIBCORO_TEST_DIR}/catch_extensions.cpp)

target_include_directories(coroTest PRIVATE ${LIBCORO_TEST_DIR})
target_link_libraries(coroTest PRIVATE libcoro ${ANDROID_LOG_LIB})
target_compile_definitions(coroTest PRIVATE LIBCORO_FEATURE_NETWORKING LIBCORO_FEATURE_TLS)
