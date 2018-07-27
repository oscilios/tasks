set (target tasks)

set (headers
    ${CMAKE_SOURCE_DIR}/include/tasks/Allocator.h
    ${CMAKE_SOURCE_DIR}/include/tasks/Queue.h
    ${CMAKE_SOURCE_DIR}/include/tasks/ThreadUtilities.h
    ${CMAKE_SOURCE_DIR}/include/tasks/Scheduler.h
    )

set (sources
    ThreadUtilities.cpp
    )


add_library (${target} ${sources} ${headers})
target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    )
if (APPLE)
    find_library (COREAUDIO_LIBRARY CoreAudio)
    target_link_libraries (${target} ${COREAUDIO_LIBRARY})
endif ()

set_target_properties (${target} PROPERTIES VERSION ${PROJECT_VERSION})

if (NOT BUILD_SHARED_LIBS)
    set_target_properties (${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

install (
    TARGETS ${target} EXPORT TaskSchedulerTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include/tasks
    )
install (
    FILES ${headers}
    DESTINATION include/tasks
    )

##### config package ####

include (CMakePackageConfigHelpers)
file (
    WRITE
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/tasks-targets.cmake\")"
    )
write_basic_package_version_file (
    "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
    )

export (
    EXPORT TaskSchedulerTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-targets.cmake"
    NAMESPACE tasks::
    )

configure_package_config_file (
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-config.cmake"
    INSTALL_DESTINATION cmake
    )

set (ConfigPackageLocation lib/cmake/tasks)

install (
    EXPORT TaskSchedulerTargets
    FILE tasks-targets.cmake
    NAMESPACE tasks::
    DESTINATION ${ConfigPackageLocation}
    )

install (
    FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/tasks-config.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/tasks/tasks-version.cmake"
    DESTINATION ${ConfigPackageLocation}
    )
