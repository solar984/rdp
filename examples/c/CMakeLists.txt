cmake_minimum_required(VERSION 3.16)

project(rdp_c_examples C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT TARGET rdplib)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../rdplib"
        "${CMAKE_CURRENT_BINARY_DIR}/rdplib"
        EXCLUDE_FROM_ALL
    )
endif()

add_executable(rdp_hello_server hello_server.c)
target_link_libraries(rdp_hello_server PRIVATE rdplib)

add_executable(rdp_hello_client hello_client.c)
target_link_libraries(rdp_hello_client PRIVATE rdplib)

add_executable(rdplib_hello_server rdplib_server.c)
target_link_libraries(rdplib_hello_server PRIVATE rdplib)

add_executable(rdplib_hello_client rdplib_client.c)
target_link_libraries(rdplib_hello_client PRIVATE rdplib)
