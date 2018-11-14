cmake_minimum_required(VERSION 3.12)
project(sensorsmonitor C)

set(CMAKE_C_STANDARD 11)

link_libraries(sensors)

add_executable(sensorsmonitor sensorsmonitor.c)

install(TARGETS sensorsmonitor
        RUNTIME DESTINATION bin)