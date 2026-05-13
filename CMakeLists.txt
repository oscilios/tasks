cmake_minimum_required(VERSION 3.30)

project(
  tasks
  VERSION 0.2
  LANGUAGES "CXX")

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(
  ${PROJECT_NAME}
  INTERFACE FILE_SET
            public_headers
            TYPE
            HEADERS
            BASE_DIRS
            include
            FILES
            include/tasks/Allocator.h
            include/tasks/MPMCSequencer.h
            include/tasks/Queue.h
            include/tasks/Scheduler.h)

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${PROJECT_VERSION})

add_subdirectory(example)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()

install(
  TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}Targets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES
  DESTINATION include
  PUBLIC_HEADER DESTINATION include/tasks FILE_SET public_headers)

include(CMakePackageConfigHelpers)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake.in
     "include(\"\${CMAKE_CURRENT_LIST_DIR}/tasks-targets.cmake\")")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

export(
  EXPORT ${PROJECT_NAME}Targets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-targets.cmake"
  NAMESPACE tasks::)

configure_package_config_file(
  ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-config.cmake"
  INSTALL_DESTINATION cmake)

set(ConfigPackageLocation lib/cmake/tasks)

install(
    EXPORT ${PROJECT_NAME}Targets
  FILE tasks-targets.cmake
  NAMESPACE tasks::
  DESTINATION ${ConfigPackageLocation})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake
              "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-version.cmake"
        DESTINATION ${ConfigPackageLocation})
