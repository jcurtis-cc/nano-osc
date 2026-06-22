cmake_minimum_required(VERSION 3.20)
project(nanoosc_examples LANGUAGES CXX)

# If we weren't added from the root, we need the library. Prefer a sibling checkout,
# otherwise allow overriding with -DLIB_FROM_SOURCE_DIR=/path/to/repo/root.
if(NOT TARGET nanoosc::nanoosc)
    get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    if(EXISTS "${_repo_root}/src/nano-osc.cpp" AND EXISTS "${_repo_root}/inc/nano-osc.hpp")
        # Avoid recursion: the root would add(examples) again.
        set(BUILD_EXAMPLES OFF CACHE BOOL "Disable nested examples when included as subproject" FORCE)
        add_subdirectory("${_repo_root}" "${CMAKE_BINARY_DIR}/_nanoosc_build" EXCLUDE_FROM_ALL)
    elseif(DEFINED LIB_FROM_SOURCE_DIR)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Disable nested examples when included as subproject" FORCE)
        add_subdirectory("${LIB_FROM_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_nanoosc_build" EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR
            "Could not find nanoosc sources. Either run from inside the repo, or pass "
            "-DLIB_FROM_SOURCE_DIR=/path/to/repo/root")
    endif()
endif()

# Tiny helper so per-example CMakeLists stay clean.
function(add_nanoosc_example name)
    if(NOT TARGET nanoosc::udp)
        message(FATAL_ERROR "${name} requires UDP. Configure with -DNANOOSC_BUILD_UDP=ON")
    endif()
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nanoosc::udp)
    set_target_properties(${name} PROPERTIES FOLDER "examples")
endfunction()

function(add_nanoosc_runtime_stats_example name)
    if(NOT TARGET nanoosc::runtime_stats)
        message(FATAL_ERROR "${name} requires runtime stats. Configure with -DNANOOSC_BUILD_RUNTIME_STATS=ON")
    endif()
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nanoosc::runtime_stats)
    set_target_properties(${name} PROPERTIES FOLDER "examples")
endfunction()

# Optional: one aggregate target for convenience.
add_custom_target(examples)

# Add each example directory.
if(TARGET nanoosc::udp)
    add_subdirectory(osc-client)
    add_subdirectory(osc-server)
else()
    message(STATUS "Skipping UDP examples because nanoosc::udp is not enabled")
endif()

if(TARGET nanoosc::runtime_stats)
    add_subdirectory(osc-stats-server)
else()
    message(STATUS "Skipping OSCStatsServer example because NANOOSC_BUILD_RUNTIME_STATS=OFF")
endif()
