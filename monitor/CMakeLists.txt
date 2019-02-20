cmake_minimum_required(VERSION 3.8)
project(monitor C)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)

link_directories(${CMAKE_BINARY_DIR}/3rdparty/zlog/src)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_DEFAULT_SOURCE")
INCLUDE_DIRECTORIES(../include  ../3rdparty/ ../3rdparty/zlog/src)

AUX_SOURCE_DIRECTORY(src/ SRC_LIST)

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME} common sqlite pthread dl zlog)

add_dependencies(${PROJECT_NAME} common sqlite zlog-lib)



file(GLOB TEST_SRCS test/*.c)
add_custom_target(${PROJECT_NAME}AllTests DEPENDS ${PROJECT_NAME})

foreach (file ${TEST_SRCS})
    get_filename_component(TEST_NAME ${file} NAME_WE)
    set(TEST_NAME ${PROJECT_NAME}_${TEST_NAME})

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_SOURCE_DIR}/${PROJECT_NAME}/src -I${CMAKE_SOURCE_DIR}/3rdparty/fff -I${CMAKE_SOURCE_DIR}/3rdparty/tap.c")
	
    add_executable(${TEST_NAME}_test ${file} ${CMAKE_SOURCE_DIR}/3rdparty/tap.c/tap.c)
    add_dependencies(${TEST_NAME}_test common sqlite zlog-lib)
    set_target_properties(${TEST_NAME}_test PROPERTIES CMAKE_C_FLAGS "-fprofile-arcs -ftest-coverage -Wno-error")
    set_target_properties(${TEST_NAME}_test PROPERTIES LINK_FLAGS "-L${CMAKE_BIN_DIR}../3rdparty/zlog/src -fprofile-arcs -ftest-coverage -z muldefs ${CMAKE_EXE_LINKER_FLAGS} -Wl,--whole-archive  -lgcov -lpthread -Wl,--no-whole-archive ")

add_test(${TEST_NAME} ${TEST_NAME}_test)

add_custom_command(
     TARGET ${TEST_NAME}_test
     COMMENT "Run tests ${CMAKE_BINARY_DIR}/${PROJECT_NAME}"
     POST_BUILD 
     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME}
     COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIGURATION> -R "^${TEST_NAME}$" --output-on-failures
)

   target_link_libraries(${TEST_NAME}_test common sqlite pthread dl zlog)

endforeach ()

