# patch/EnvelopeOscillator/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)
project(EnvelopeOscillator LANGUAGES C CXX)

# Specify the linker script path relative to the project root
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32H750IB_flash.lds")

# Collect all necessary source files
set(SOURCES
        EnvelopeOscillator.cpp
        ${CMAKE_SOURCE_DIR}/libDaisy/src/sys/system_stm32h7xx.c
        ${CMAKE_SOURCE_DIR}/syscalls.c
        # Add other system or library source files as needed
)

# Create the executable (this will produce the .elf)
add_executable(EnvelopeOscillator
        ${SOURCES}
        # Add other source files here if necessary
)

# Link against libDaisy and DaisySP
target_link_libraries(EnvelopeOscillator
        PRIVATE
        daisy
        DaisySP
        c
        m
        nosys
)

# Optionally, include FATFS sources if USE_FATFS is enabled
if(USE_FATFS)
    set(FATFS_DIR "${CMAKE_SOURCE_DIR}/libDaisy/Middlewares/Third_Party/FatFs/src")
    list(APPEND SOURCES
            ${FATFS_DIR}/diskio.c
            ${FATFS_DIR}/ff.c
            ${FATFS_DIR}/ff_gen_drv.c
            ${FATFS_DIR}/option/ccsbcs.c
    )
    include_directories(${FATFS_DIR})
endif()

# Add C definitions
target_compile_definitions(EnvelopeOscillator PRIVATE
        USE_HAL_DRIVER
        STM32H750xx
        HSE_VALUE=16000000
        CORE_CM7
        STM32H750IB
        ARM_MATH_CM7
        USE_FULL_LL_DRIVER
        # Add more definitions as needed
)

# Conditional Definitions
if(DEBUG)
    target_compile_definitions(EnvelopeOscillator PRIVATE DEBUG)
endif()

if(USE_DAISYSP_LGPL)
    target_compile_definitions(EnvelopeOscillator PRIVATE USE_DAISYSP_LGPL)
endif()

# Specify include directories
target_include_directories(EnvelopeOscillator PRIVATE
        "${CMAKE_SOURCE_DIR}/libDaisy"
        "${CMAKE_SOURCE_DIR}/libDaisy/src/"
        "${CMAKE_SOURCE_DIR}/libDaisy/src/sys"
        "${CMAKE_SOURCE_DIR}/libDaisy/src/usbd"
        "${CMAKE_SOURCE_DIR}/libDaisy/src/usbh"
        "${CMAKE_SOURCE_DIR}/libDaisy/Drivers/CMSIS/Include/"
        "${CMAKE_SOURCE_DIR}/libDaisy/Drivers/CMSIS/DSP/Include/"
        "${CMAKE_SOURCE_DIR}/libDaisy/Drivers/CMSIS/Device/ST/STM32H7xx/Include/"
        "${CMAKE_SOURCE_DIR}/libDaisy/Drivers/STM32H7xx_HAL_Driver/Inc/"
        "${CMAKE_SOURCE_DIR}/libDaisy/Middlewares/ST/STM32_USB_Device_Library/Core/Inc"
        "${CMAKE_SOURCE_DIR}/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Core/Inc"
        "${CMAKE_SOURCE_DIR}/libDaisy/Middlewares/ST/STM32_USB_Host_Library/Class/MSC/Inc"
        "${CMAKE_SOURCE_DIR}/libDaisy/Middlewares/Third_Party/FatFs/src"
        "${CMAKE_SOURCE_DIR}/"
)

# Specify linker options, including the linker script
target_link_options(EnvelopeOscillator PRIVATE
        "-Wl,--gc-sections"
        "-T${LINKER_SCRIPT}"
        "--specs=nano.specs"
        "--specs=nosys.specs"
        "-Wl,-Map=${CMAKE_BINARY_DIR}/EnvelopeOscillator.map,--cref"
)

# Define where the .bin will be generated
set(EnvelopeOscillator_ELF $<TARGET_FILE:EnvelopeOscillator>)
set(EnvelopeOscillator_BIN "${CMAKE_CURRENT_BINARY_DIR}/EnvelopeOscillator.bin")

# Custom command to convert .elf to .bin using objcopy
add_custom_command(
        OUTPUT ${EnvelopeOscillator_BIN}
        COMMAND arm-none-eabi-objcopy -O binary ${EnvelopeOscillator_ELF} ${EnvelopeOscillator_BIN}
        DEPENDS EnvelopeOscillator
        COMMENT "Converting ELF to BIN for EnvelopeOscillator"
)

# Custom target to program the device via DFU
add_custom_target(EnvelopeOscillator-dfu
        COMMAND dfu-util -a 0 -s 0x08000000:leave -D ${EnvelopeOscillator_BIN} -d ,0483:df11
        DEPENDS ${EnvelopeOscillator_BIN}
        COMMENT "Flashing EnvelopeOscillator.bin via DFU"
)