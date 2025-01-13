cmake_minimum_required(VERSION 3.20)
# Give the overall project a name
project(DaisyExamples LANGUAGES C CXX)

# Just in case, specify C++ standard globally
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Add subdirectories:
add_subdirectory(libDaisy)
add_subdirectory(DaisySP)
add_subdirectory(pod/EuclideanDrums2)
add_subdirectory(pod/Drums)
add_subdirectory(patch/Sequencer)
add_subdirectory(patch/Noise)

# Define common compiler flags
set(COMMON_C_FLAGS
        "-mcpu=cortex-m7"
        "-mthumb"
        "-mfpu=fpv5-d16"
        "-mfloat-abi=hard"
        "-O2"
        "-Wall"
        "-Wextra"
        "-fdata-sections"
        "-ffunction-sections"
        "-fasm"
        "-finline"
        "-finline-functions-called-once"
        "-fshort-enums"
        "-fno-move-loop-invariants"
        "-fno-unwind-tables"
        "-fno-rtti"
        "-Wno-register"
        "-Wno-missing-attributes"
        "-Wno-stringop-overflow"
)

# Apply to all C targets
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_C_FLAGS}")

# Apply to all C++ targets
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_C_FLAGS} -fno-exceptions -fno-rtti")
