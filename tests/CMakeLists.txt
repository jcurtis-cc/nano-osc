add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest
)

add_executable(nano-osc-tests
	test_main.cpp
	test_roundtrip.cpp
	test_malformed.cpp
	test_server.cpp
	test_views.cpp
	test_zero_alloc.cpp
)

target_link_libraries(nano-osc-tests
	PRIVATE
		nanoosc::runtime
		doctest
)

add_test(NAME unit COMMAND nano-osc-tests)
