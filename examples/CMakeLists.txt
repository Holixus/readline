cmake_minimum_required(VERSION 3.0)

IF (BUILD_EXAMPLES)
    PROJECT(readline-examples C)

    INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/..)
    LINK_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/..)

    ADD_EXECUTABLE(example example.c)
    TARGET_LINK_LIBRARIES(example readline)
ENDIF()
