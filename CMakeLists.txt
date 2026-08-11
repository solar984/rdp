cmake_minimum_required(VERSION 3.16)

project(rdp)

include(CTest)

add_subdirectory(rdplib)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
