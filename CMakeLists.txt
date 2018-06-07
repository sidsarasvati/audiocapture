cmake_minimum_required(VERSION 2.8.9)
project (audiocapture)

# link with libsoundio
set(PROJECT_LINK_LIBS libsoundio.a)
link_directories(lib)


include_directories(include/soundio)
file(GLOB SOURCES "src/*.cpp")
add_executable(audiocapture ${SOURCES})
target_link_libraries(audiocapture ${PROJECT_LINK_LIBS})
