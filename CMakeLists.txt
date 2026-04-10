cmake_minimum_required(VERSION 3.13)

project(http-server)

file(GLOB_RECURSE SOURCE_FILES src/*.c)

include_directories(include)

# set(CMAKE_C_STANDARD 23) # Enable the C23 standard

add_executable(http-server ${SOURCE_FILES})

target_link_libraries(http-server z)

set(-Wno-format-truncation true)
set(-Wno-stringop-overread true)
