# Set minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Set project name
project(ST7735S_Project)

# Define the executable output
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)

# Add executable for your example main file
add_executable(example_exec examples/Test.c)

# Add the source files for your project
target_sources(example_exec PRIVATE
    gfx.c                  # Graphics source file
    st7735s.c              # ST7735S driver source file
    st7735s_compat.c       # ST7735S compatibility source file
    fonts.c                # Font handling source file (inside utils/fonts)
)

# Include directories for header files
target_include_directories(example_exec PRIVATE
    utils                  # For fonts headers in utils
    utils/fonts            # For gfx, st7735s, st7735s_compat headers in the main directory
)

# Link the gpiod library for GPIO functionality
target_link_libraries(example_exec PRIVATE gpiod)

# Optionally, if you need to link with other external libraries, you can add them like:
# target_link_libraries(example_exec PRIVATE <external_libraries>)

# Set compile options (if needed)
# For example, to define certain preprocessor directives:
# target_compile_definitions(example_exec PRIVATE YOUR_DEFINE_HERE)

# Optionally set C standard, e.g. to force C99 or later
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)




