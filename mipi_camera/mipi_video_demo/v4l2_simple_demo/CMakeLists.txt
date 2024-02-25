cmake_minimum_required(VERSION 3.10)

project(opencv4_demo)

# 寻找OpenCV
find_package(OpenCV 4 REQUIRED)

# 添加可执行文件
add_executable(opencv4_demo firefly_v4l2_simple_demo.cpp)

# 链接OpenCV库
target_link_libraries(opencv4_demo ${OpenCV_LIBS})
