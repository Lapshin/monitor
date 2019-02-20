cmake_minimum_required(VERSION 2.8)

add_subdirectory(sqlite)


add_custom_target(zlob-bin-dir
                   COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/zlog
)

add_custom_target(zlog-lib
                   DEPENDS zlob-bin-dir
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/zlog
                   COMMAND rsync -auvv ${CMAKE_CURRENT_SOURCE_DIR}/zlog ${CMAKE_CURRENT_BINARY_DIR} 2>&1 >/dev/null 
                   COMMAND make
)