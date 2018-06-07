cmake_minimum_required(VERSION 2.8.9)
project (audiocapture)

file(GLOB SOURCES "src/*.cpp")
add_executable(audiocapture ${SOURCES})
