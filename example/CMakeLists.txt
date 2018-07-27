set (target task_example)
add_executable (${target} main.cpp)
target_include_directories (${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    )
target_link_libraries (${target} tasks)
