
# toolchain.cmake

# Specify the cross-compiling environment
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m7)

# Specify the cross compiler executables
set(CMAKE_C_COMPILER "C:/Program Files/DaisyToolchain/bin/arm-none-eabi-gcc.exe")
set(CMAKE_CXX_COMPILER "C:/Program Files/DaisyToolchain/bin/arm-none-eabi-g++.exe")
set(CMAKE_ASM_COMPILER "C:/Program Files/DaisyToolchain/bin/arm-none-eabi-gcc.exe")

# Compiler flags
set(CMAKE_C_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -Wall -Wextra -fdata-sections -ffunction-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-exceptions -fno-rtti")

# Prevent CMake from trying to link during the test compile
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Optionally, specify the linker script here if you prefer
# set(LINKER_SCRIPT "C:/Users/nrosq/CLionProjects/DaisyExamples/libDaisy/core/STM32H750IB_flash.lds")
# set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections -T${LINKER_SCRIPT}")