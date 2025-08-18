cmake_minimum_required(VERSION 3.20)
project(nanoosc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release" FORCE)
endif()

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

add_library(nanoosc src/nano-osc.cpp)

target_include_directories(nanoosc
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
)

# Namespaced alias 
add_library(nanoosc::nanoosc ALIAS nanoosc)

option(BUILD_EXAMPLES "Build examples during the build" ON)
if(BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

