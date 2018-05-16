cmake_minimum_required(VERSION 3.7)
project(3_ksqsf)

set(CMAKE_C_STANDARD 11)
link_libraries(-lfuse)
add_definitions(-D_FILE_OFFSET_BITS=64 -O2)
add_executable(oshfs main.c oshfs.c oshfs.h config.h bitmap.c bitmap.h)
