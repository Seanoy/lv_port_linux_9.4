# Usage:
# cmake -DCMAKE_TOOLCHAIN_FILE=./user_cross_compile_setup.cmake -B build -S .
# make  -C build -j

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(tools /home/sean/cv184x_sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-linux-musleabihf)
set(CMAKE_C_COMPILER ${tools}/bin/arm-none-linux-musleabihf-gcc)
set(CMAKE_CXX_COMPILER ${tools}/bin/arm-none-linux-musleabihf-g++)

# If necessary, set STAGING_DIR
# if not work, please try(in shell command): export STAGING_DIR=/home/ubuntu/Your_SDK/out/xxx/openwrt/staging_dir/target
#set(ENV{STAGING_DIR} "/home/ubuntu/Your_SDK/out/xxx/openwrt/staging_dir/target")

