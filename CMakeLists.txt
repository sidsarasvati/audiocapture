cmake_minimum_required(VERSION 2.8.9)
project (audiocapture)

set(CMAKE_CXX_FLAGS "-Wall -std=c++11")
message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")

# ----------------------------------------------------------
# find all external libraries to link with

find_library(AUDIO_TOOLBOX AudioToolbox)
if (NOT AUDIO_TOOLBOX)
    message(FATAL_ERROR "AudioToolbox not found")
endif()
find_library(AUDIO_UNIT AudioUnit)
if (NOT AUDIO_UNIT)
    message(FATAL_ERROR "AudioUnit not found")
endif()
find_library(CORE_AUDIO CoreAudio)
if (NOT CORE_AUDIO)
    message(FATAL_ERROR "CoreAudio not found")
endif()
find_library(CORE_FOUNDATION CoreFoundation)
if (NOT CORE_AUDIO)
    message(FATAL_ERROR "CoreFoundation not found")
endif()
# find_library(SOUNDIO soundio)
# if (NOT FOO)
#     message(FATAL_ERROR "soundio not found")
# endif()

# ----------------------------------------------------------

set(PROJECT_LINK_LIBS
  libsoundio.a
  ${AUDIO_TOOLBOX}
  ${AUDIO_UNIT}
  ${CORE_AUDIO}
  ${CORE_FOUNDATION})


link_directories(lib) # todo - replace with find_library
include_directories(include)

file(GLOB SOURCES "src/*.cpp")
add_executable(audiocapture ${SOURCES})
target_link_libraries(audiocapture ${PROJECT_LINK_LIBS})
