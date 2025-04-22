# Set minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Set project name
project(ST7735S_Project)

# Define the executable output
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

# Add executable for your example main file (now C++)
add_executable(example_exec Application_code/Test.cpp)

# Add the source files for your project
target_sources(example_exec PRIVATE
    gfx.c                  # Graphics source file
    st7735s.c              # ST7735S driver source file
    st7735s_compat.c       # ST7735S compatibility source file
    fonts.c                # Font handling source file
    appgpio.c              # GPIO handling source file
)

# Include directories for header files
target_include_directories(example_exec PRIVATE
    utils                  # For fonts headers
    utils/fonts            # For gfx, st7735s, st7735s_compat headers
)

# Link necessary libraries
target_link_libraries(example_exec PRIVATE 
    gpiod    # GPIO control library
    pthread  # Threading support
    rt       # Real-time clock support (needed for clock_gettime)
    m        # Math functions
)

# Set C and C++ standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 17)  # Ensure C++17 for Test.cpp
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Ensure C files are compiled with C compiler even in a C++ project
set_source_files_properties(
    gfx.c st7735s.c st7735s_compat.c fonts.c
    PROPERTIES LANGUAGE C
)

# Print final linking status
message(STATUS "Linking libraries: gpiod, pthread, rt, m")
