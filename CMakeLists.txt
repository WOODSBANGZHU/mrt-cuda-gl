cmake_minimum_required(VERSION 3.21)
project(mrt_cuda_gl LANGUAGES CXX)

# Configure with the FFmpeg and CUDA SDK roots used by the deployment image:
# cmake -S native/cuda_gl -B build/cuda-gl -DFFMPEG_ROOT=C:/ffmpeg
# cmake --build build/cuda-gl --config Release

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)
if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
endif()

# The bridge only needs JNI headers. Find them explicitly because CMake's
# FindJNI also requires AWT/JVM libraries, which are not present in minimal
# CUDA build containers even when a full JDK is installed.
find_path(JNI_INCLUDE_DIR jni.h
        HINTS
        "$ENV{JAVA_HOME}"
        /usr/lib/jvm/default-java
        /usr/lib/jvm/java-17-openjdk-amd64
        PATH_SUFFIXES include)
find_path(JNI_PLATFORM_INCLUDE_DIR jni_md.h
        HINTS
        "$ENV{JAVA_HOME}"
        /usr/lib/jvm/default-java
        /usr/lib/jvm/java-17-openjdk-amd64
        PATH_SUFFIXES include/linux include)
if (NOT JNI_INCLUDE_DIR OR NOT JNI_PLATFORM_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find JNI headers (jni.h and jni_md.h); set JAVA_HOME to a JDK")
endif()

if (UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
endif()

add_library(mrt_cuda_gl SHARED mrt_cuda_gl.cpp)
target_include_directories(mrt_cuda_gl PRIVATE
        ${JNI_INCLUDE_DIR}
        ${JNI_PLATFORM_INCLUDE_DIR}
        ${CUDAToolkit_INCLUDE_DIRS})
if (UNIX AND NOT APPLE)
    # Embed the CUDA runtime so Ubuntu deployment only needs the NVIDIA driver.
    target_link_libraries(mrt_cuda_gl PRIVATE CUDA::cudart_static Threads::Threads ${CMAKE_DL_LIBS} m)
else()
    target_link_libraries(mrt_cuda_gl PRIVATE CUDA::cudart)
endif()

if (DEFINED FFMPEG_ROOT AND NOT "${FFMPEG_ROOT}" STREQUAL "")
    target_include_directories(mrt_cuda_gl PRIVATE ${FFMPEG_ROOT}/include)
    find_library(AVUTIL_LIBRARY
            NAMES avutil
            PATHS ${FFMPEG_ROOT}/lib
            NO_DEFAULT_PATH
            REQUIRED)
    target_link_libraries(mrt_cuda_gl PRIVATE ${AVUTIL_LIBRARY})
elseif (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
    target_link_libraries(mrt_cuda_gl PRIVATE PkgConfig::AVUTIL)
else()
    message(FATAL_ERROR "FFMPEG_ROOT must point to an FFmpeg development package")
endif()

if (WIN32)
    set_target_properties(mrt_cuda_gl PROPERTIES PREFIX "" SUFFIX ".dll")
    target_link_libraries(mrt_cuda_gl PRIVATE opengl32)
elseif (UNIX AND NOT APPLE)
    target_link_libraries(mrt_cuda_gl PRIVATE OpenGL::GL dl)
endif()
