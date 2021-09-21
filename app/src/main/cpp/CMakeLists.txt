cmake_minimum_required(VERSION 3.10.2)


project("pusherdemo")

# 引入子目录的CmakeList.txt rmtp静态库
add_subdirectory(librtmp)

#导入头文件的路径 自动查看头文件 <>
include_directories(librtmp libx264/include)

# 只要有一个C++文件，就是属于C++ == CXX CMAKE_CXX_FLAGS
# set(CMAKE_CXX_FLAGS)

# 设置x264库路径
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L${CMAKE_SOURCE_DIR}/libx264/libs/${CMAKE_ANDROID_ARCH_ABI}")


add_library( # Sets the name of the library.
        native-lib

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        native-lib.cpp
        VideoChannel.cpp)


find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

target_link_libraries( # Specifies the target library.
        native-lib
        rtmp
        x264
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})