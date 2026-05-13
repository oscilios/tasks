set(target task_tests)
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.10.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(${target} task_test.cpp)
target_link_libraries(${target} tasks::tasks Catch2WithMain)
add_test(NAME ${target} COMMAND ${target})
