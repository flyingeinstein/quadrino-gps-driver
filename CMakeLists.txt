cmake_minimum_required(VERSION 2.8)

project(gps_quadrino_test C)

set(CMAKE_C_FLAGS "-std=gnu89")

include_directories(/usr/include)
include_directories(/usr/local/include)

set(SOURCE_FILES test.c nmea.c)

add_executable(gps_quadrino_test ${SOURCE_FILES})

