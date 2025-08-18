add_nanoosc_example(osc-server src/osc-server.cpp)

    target_include_directories(osc-server PRIVATE ${CMAKE_CURRENT_LIST_DIR}/inc)

#Add to aggregate target
        add_dependencies(examples osc-server)
