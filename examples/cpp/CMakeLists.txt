cmake_minimum_required(VERSION 3.16)

project(rdp_cpp_example C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET rdplib)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../rdplib"
        "${CMAKE_CURRENT_BINARY_DIR}/rdplib"
        EXCLUDE_FROM_ALL
    )
endif()

set(RDP_CPP_COMMON_SOURCES
    common/application_packet.cpp
    common/rdp_connection.cpp
    common/rdp_endpoint.cpp
    common/rdp_runtime.cpp
)

add_executable(rdplib_cpp_server
    ${RDP_CPP_COMMON_SOURCES}
    server/client.cpp
    server/client_manager.cpp
    server/main.cpp
)

add_executable(rdplib_cpp_client
    ${RDP_CPP_COMMON_SOURCES}
    client/game_manager.cpp
    client/main.cpp
    client/server_manager.cpp
)

target_include_directories(rdplib_cpp_server PRIVATE common server)
target_include_directories(rdplib_cpp_client PRIVATE common client)

target_link_libraries(rdplib_cpp_server PRIVATE rdplib)
target_link_libraries(rdplib_cpp_client PRIVATE rdplib)

if(MSVC)
    target_compile_options(rdplib_cpp_server PRIVATE /W4)
    target_compile_options(rdplib_cpp_client PRIVATE /W4)
else()
    target_compile_options(rdplib_cpp_server PRIVATE -Wall -Wextra)
    target_compile_options(rdplib_cpp_client PRIVATE -Wall -Wextra)
endif()
