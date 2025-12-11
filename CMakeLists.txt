cmake_minimum_required(VERSION 3.10) 
project(key_interceptor)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)


file(GLOB_RECURSE SOURCES "src/*.cpp")
# Add the main executable
add_executable(${PROJECT_NAME} ${SOURCES})
        

find_package(spdlog)
find_package(fmt)
find_package(glm)
target_link_libraries(${PROJECT_NAME} spdlog::spdlog fmt::fmt glm::glm)
