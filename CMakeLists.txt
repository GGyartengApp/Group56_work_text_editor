cmake_minimum_required(VERSION 3.10)
project(Group56_Work)

# Add ImGui source files
file(GLOB IMGUI_SOURCES
    external/imgui/*.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ask wx-config for flags
execute_process(
        COMMAND wx-config --cxxflags
        OUTPUT_VARIABLE WX_CXXFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
        COMMAND wx-config --libs all,stc
        OUTPUT_VARIABLE WX_LIBS
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Using wxWidgets compile flags: ${WX_CXXFLAGS}")
message(STATUS "Using wxWidgets link flags: ${WX_LIBS}")

find_package(CURL REQUIRED)
add_executable(Group56_Work main.cpp)

# Include directory for nlohmann JSON
target_include_directories(Group56_Work PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Split only compile flags; do not split WX_LIBS to preserve -framework flags
separate_arguments(WX_CXXFLAGS)

target_compile_options(Group56_Work PRIVATE ${WX_CXXFLAGS})

find_package(OpenGL REQUIRED)
target_link_libraries(Group56_Work PRIVATE ${WX_LIBS} imgui OpenGL::GL CURL::libcurl)
message(STATUS "ImGui and OpenGL integrated")


# Copy resources folder to build directory
file(COPY Resources DESTINATION ${CMAKE_BINARY_DIR})