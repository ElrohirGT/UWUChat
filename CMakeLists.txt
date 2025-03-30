cmake_minimum_required(VERSION 3.27)
project(uwu_client C)
set(CMAKE_C_STANDARD 99)

# Set the C standard
# Add Raylib (from deps/raylib)
add_subdirectory(deps/raylib)
add_subdirectory(deps/facil.io)

# Define the executable
add_executable(${PROJECT_NAME} src/client.c src/lib.c)
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE raylib facil.io)

add_custom_command(
        TARGET uwu_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
        ${CMAKE_CURRENT_BINARY_DIR}/src/resources)