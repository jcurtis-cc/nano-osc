cmake_minimum_required(VERSION 3.20)
project(nanoosc VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release" FORCE)
endif()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

add_library(nanoosc_core src/nano-osc.cpp)

target_include_directories(nanoosc_core
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
		$<INSTALL_INTERFACE:include>
)
target_compile_features(nanoosc_core PUBLIC cxx_std_17)

add_library(nanoosc::core ALIAS nanoosc_core)

option(NANOOSC_BUILD_RUNTIME "Build transport-agnostic client/server runtime battery" ON)
if(NANOOSC_BUILD_RUNTIME)
	add_library(nanoosc_runtime src/runtime.cpp)
	target_link_libraries(nanoosc_runtime PUBLIC nanoosc::core)
	target_compile_features(nanoosc_runtime PUBLIC cxx_std_17)
	add_library(nanoosc::runtime ALIAS nanoosc_runtime)
endif()

set(_nanoosc_default_udp ON)
if(WIN32)
	set(_nanoosc_default_udp OFF)
endif()
option(NANOOSC_BUILD_UDP "Build POSIX UDP transport" ${_nanoosc_default_udp})
if(NANOOSC_BUILD_UDP)
	if(NOT NANOOSC_BUILD_RUNTIME)
		message(FATAL_ERROR "NANOOSC_BUILD_UDP requires NANOOSC_BUILD_RUNTIME=ON")
	endif()
	add_library(nanoosc_udp src/udp_transport_posix.cpp)
	target_link_libraries(nanoosc_udp PUBLIC nanoosc::runtime)
	target_compile_features(nanoosc_udp PUBLIC cxx_std_17)
	add_library(nanoosc::udp ALIAS nanoosc_udp)
endif()

# Convenience umbrella target
add_library(nanoosc INTERFACE)
target_link_libraries(nanoosc INTERFACE nanoosc::core)
if(TARGET nanoosc::runtime)
	target_link_libraries(nanoosc INTERFACE nanoosc::runtime)
endif()
if(TARGET nanoosc::udp)
	target_link_libraries(nanoosc INTERFACE nanoosc::udp)
endif()
# Namespaced alias
add_library(nanoosc::nanoosc ALIAS nanoosc)

option(BUILD_EXAMPLES "Build examples during the build" ON)
if(BUILD_EXAMPLES)
	if(TARGET nanoosc::udp)
		add_subdirectory(examples)
	else()
		message(STATUS "Skipping examples because NANOOSC_BUILD_UDP=OFF")
	endif()
endif()

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
	if(TARGET nanoosc::runtime)
		enable_testing()
		add_subdirectory(tests)
	else()
		message(STATUS "Skipping tests because NANOOSC_BUILD_RUNTIME=OFF")
	endif()
endif()
