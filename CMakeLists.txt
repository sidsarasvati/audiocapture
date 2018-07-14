cmake_minimum_required(VERSION 3.2)
set (CMAKE_CXX_STANDARD 11)
project (audiocapture)

set(CMAKE_CXX_FLAGS "-Wall" CACHE INTERNAL "" FORCE)
message("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")

set(DSP_PATH "${PROJECT_SOURCE_DIR}/src/dsp")
set(BIN_PATH "${PROJECT_SOURCE_DIR}/src/bin")

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

function(create_object_library lib_name)
    # building an object files library prevents
    # CMAKE from having to build all source files
    # independently for static and shared libraries
    add_library(${lib_name} OBJECT ${ARGN})
    set_target_properties(${lib_name}
        PROPERTIES
            CXX_STANDARD 11
            POSITION_INDEPENDENT_CODE 1)
endfunction()

# ----------------------------------------------------------
include_directories("${PROJECT_SOURCE_DIR}/src")

# ----------------------------------------------------------

set(src_DSP_all
    ${DSP_PATH}/cheby1.c++
    ${DSP_PATH}/cheby1.h++
    ${DSP_PATH}/directform2.h++
    ${DSP_PATH}/resampler.c++
    ${DSP_PATH}/resampler.h++
)

create_object_library(DSP_object ${src_DSP_all})

# ----------------------------------------------------------

add_executable(resample_pcm
        "${BIN_PATH}/resample_pcm.c++"
        $<TARGET_OBJECTS:DSP_object>
    )

# ----------------------------------------------------------

set(PROJECT_LINK_LIBS
  libsoundio.a
  ${AUDIO_TOOLBOX}
  ${AUDIO_UNIT}
  ${CORE_AUDIO}
  ${CORE_FOUNDATION})

link_directories("${PROJECT_SOURCE_DIR}/lib")

set(src_audiocapture_all
    ${DSP_PATH}/cheby1.c++
    ${DSP_PATH}/cheby1.h++
    ${DSP_PATH}/directform2.h++
    ${DSP_PATH}/resampler.c++
    ${DSP_PATH}/resampler.h++
)

add_executable(audiocapture
        "${BIN_PATH}/audiocapture.c++"
        $<TARGET_OBJECTS:DSP_object>
    )

target_link_libraries(audiocapture ${PROJECT_LINK_LIBS})
