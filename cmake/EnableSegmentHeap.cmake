# The segment-heap manifest makes the MSVC test executables abort with
# STATUS_STACK_BUFFER_OVERRUN (0xc0000409) as soon as CMake launches them
# for gtest discovery. The application keeps the heap.
if(BUILD_TESTS OR BUILD_BENCHMARKS)
    set(ENV{VS_SEGMENT_HEAP_EXCLUDE} "twitch-eventsub-ws-test;chatterino-test;chatterino-benchmark")
endif()

# A test-only configure never builds the application. Skip the Visual Studio
# script so a later toolset cannot opt those binaries back in.
if(BUILD_TESTS AND NOT BUILD_APP AND NOT BUILD_BENCHMARKS)
    message(STATUS "Skipping segment heap for the test-only build.")
    return()
endif()

if(NOT DEFINED ENV{VSINSTALLDIR})
    message(WARNING "Missing VSINSTALLDIR environment variable - not enabling segment heap.")
    return()
endif()

set(segment_heap_path "$ENV{VSINSTALLDIR}Common7/IDE/CommonExtensions/Microsoft/CMake/cmake/Microsoft/SegmentHeap.cmake")
if(NOT EXISTS "${segment_heap_path}")
    message(STATUS "Missing '${segment_heap_path}'. Segment heap will be disabled - consider updating Visual Studio.")
    return()
endif()

message(STATUS "Including segment heap script from '${segment_heap_path}'.")
include("${segment_heap_path}")
