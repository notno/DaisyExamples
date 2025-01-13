# pod/EuclideanDrums2/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)
project(EuclideanDrums2 LANGUAGES C CXX)

# Specify the linker script path relative to the project root
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/libDaisy/core/STM32H750IB_flash.lds")

# Create the executable (this will produce the .elf)
add_executable(EuclideanDrums2
        EuclideanDrums2.cpp
        # Add other source files here if necessary
)

# Link against libDaisy and DaisySP
target_link_libraries(EuclideanDrums2
        PRIVATE
        daisy
        DaisySP
)

# Specify linker options, including the linker script
target_link_options(EuclideanDrums2 PRIVATE
        "-Wl,--gc-sections"
        "-T${LINKER_SCRIPT}"
)

# Define where the .bin will be generated
set(EUCLIDEANDRUMS2_ELF $<TARGET_FILE:EuclideanDrums2>)
set(EUCLIDEANDRUMS2_BIN "${CMAKE_CURRENT_BINARY_DIR}/EuclideanDrums2.bin")

# Custom command to convert .elf to .bin using objcopy
add_custom_command(
        OUTPUT ${EUCLIDEANDRUMS2_BIN}
        COMMAND arm-none-eabi-objcopy -O binary ${EUCLIDEANDRUMS2_ELF} ${EUCLIDEANDRUMS2_BIN}
        DEPENDS EuclideanDrums2
        COMMENT "Converting ELF to BIN for EuclideanDrums2"
)

# Custom target to program the device via DFU
add_custom_target(EuclideanDrums2-dfu
        COMMAND dfu-util -a 0 -s 0x08000000:leave -D ${EUCLIDEANDRUMS2_BIN} -d ,0483:df11
        DEPENDS ${EUCLIDEANDRUMS2_BIN}
        COMMENT "Flashing EuclideanDrums2.bin via DFU"
)