cmake_minimum_required(VERSION 3.21)
project(mrt_cuda_gl LANGUAGES CXX)

# Configure with the FFmpeg and CUDA SDK roots used by the deployment image:
# cmake -S native/cuda_gl -B build/cuda-gl -DFFMPEG_ROOT=C:/ffmpeg
# cmake --build build/cuda-gl --config Release

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)
find_package(JNI REQUIRED)

if (UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
endif()

add_library(mrt_cuda_gl SHARED mrt_cuda_gl.cpp)
target_include_directories(mrt_cuda_gl PRIVATE
        ${JNI_INCLUDE_DIRS}
        ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(mrt_cuda_gl PRIVATE CUDA::cudart)

if (DEFINED FFMPEG_ROOT AND NOT "${FFMPEG_ROOT}" STREQUAL "")
    target_include_directories(mrt_cuda_gl PRIVATE ${FFMPEG_ROOT}/include)
    target_link_directories(mrt_cuda_gl PRIVATE ${FFMPEG_ROOT}/lib)
    target_link_libraries(mrt_cuda_gl PRIVATE avutil)
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
