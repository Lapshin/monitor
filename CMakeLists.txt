cmake_minimum_required(VERSION 3.9)

if ( ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR} )
    message( FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there. You may need to remove CMakeCache.txt." )
endif()

ENABLE_TESTING()
set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-pedantic -Wall -O2 -fPIC ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-check -fstack-protector-all -ftrapv -D_FORTIFY_SOURCE=2 ")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize=undefined -fno-sanitize-recover ")

add_subdirectory(3rdparty)
add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(monitor)