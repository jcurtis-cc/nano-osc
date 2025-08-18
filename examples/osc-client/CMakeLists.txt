add_nanoosc_example(osc-client src/osc-client.cpp)

    target_include_directories(osc-client PRIVATE ${CMAKE_CURRENT_LIST_DIR}/inc)

#Add to aggregate target
        add_dependencies(examples osc-client)
