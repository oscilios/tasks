set (target task_tests)
set (sources
    main.cpp
    task_test.cpp
    )
add_executable (${target} ${sources})
target_include_directories (${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    )
target_link_libraries (${target} tasks)
add_test (NAME ${target} COMMAND ${target} --use-colour yes)
