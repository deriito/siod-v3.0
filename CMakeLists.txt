cmake_minimum_required(VERSION 3.23)
project(siod_v3_0 C)

set(CMAKE_C_STANDARD 99)

add_executable(siod_v3_0 siod.c slib.c sliba.c trace.c)
