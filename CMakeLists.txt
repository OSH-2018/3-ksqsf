cmake_minimum_required(VERSION 3.7)
project(3_ksqsf)

set(CMAKE_CXX_STANDARD 14)
link_libraries(-lfuse)
add_definitions(-D_FILE_OFFSET_BITS=64)
add_executable(oshfs main.c oshfs.c oshfs.h config.h bitmap.c bitmap.h)
