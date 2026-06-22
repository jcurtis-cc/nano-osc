add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest
)

set(NANOOSC_TEST_SOURCES
	test_main.cpp
	test_roundtrip.cpp
	test_malformed.cpp
	test_server.cpp
	test_views.cpp
	test_zero_alloc.cpp
)

if(TARGET nanoosc::runtime_stats)
	list(APPEND NANOOSC_TEST_SOURCES test_runtime_stats.cpp)
endif()

add_executable(nano-osc-tests
	${NANOOSC_TEST_SOURCES}
)

target_link_libraries(nano-osc-tests
	PRIVATE
		doctest
)

if(TARGET nanoosc::runtime_stats)
	target_link_libraries(nano-osc-tests PRIVATE nanoosc::runtime_stats)
else()
	target_link_libraries(nano-osc-tests PRIVATE nanoosc::runtime)
endif()

add_test(NAME unit COMMAND nano-osc-tests)
