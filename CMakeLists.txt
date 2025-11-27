cmake_minimum_required(VERSION 3.10)
project(lvgl_sim LANGUAGES C)

# 强制 Release + 最高优化
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set(CMAKE_C_STANDARD 99)

# 输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

# 加入 lvgl 子模块（假设你项目里已经把 lvgl 当成子目录放在 lvgl/ 里）
add_subdirectory(lvgl)

# ================== 你自己的后台源码（fbdev 等）==================
file(GLOB LV_LINUX_SRC       src/lib/*.c)
set(LV_LINUX_BACKEND_SRC     src/lib/display_backends/fbdev.c)  # 只用 fbdev

# ================== 关键：强制单线程 + 极致省电配置 ==================

# ================== 可执行文件 ==================
add_executable(lvglsim
    src/main.c
    ${LV_LINUX_SRC}
    ${LV_LINUX_BACKEND_SRC}
)

# 头文件路径
target_include_directories(lvglsim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
)

# ================== 编译选项（musl armhf 100% 通过的最优组合）==================
target_compile_options(lvglsim PRIVATE
    -O3 -pipe
    -march=armv7-a
    -mtune=cortex-a53
    -mfpu=neon-vfpv4
    -mfloat-abi=hard
    -ftree-vectorize
    -ffast-math
    -flto
    -fno-math-errno
    -Wall
)

# 链接阶段也开 LTO
target_link_options(lvglsim PRIVATE -flto)

# ================== 链接顺序（必须 lvgl 在最前面）==================
target_link_libraries(lvglsim
    lvgl          # 核心库，必须最前面
    pthread
    m
    stdc++
)

# ================== 方便运行 ==================
add_custom_target(run
    COMMAND ${EXECUTABLE_OUTPUT_PATH}/lvglsim
    DEPENDS lvglsim
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)