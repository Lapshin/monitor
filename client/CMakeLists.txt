cmake_minimum_required(VERSION 2.8)
project(client C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
INCLUDE_DIRECTORIES(../include)

AUX_SOURCE_DIRECTORY(src/ SRC_LIST)

add_executable(${PROJECT_NAME} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} common)

add_dependencies(${PROJECT_NAME} common)
