cmake_minimum_required(VERSION 3.22)
project(pocketcentury CXX)
set(CMAKE_CXX_STANDARD 20)
set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core)
file(GLOB UNSP ${CORE}/unsp/unsp*.cpp)
list(FILTER UNSP EXCLUDE REGEX "unspdasm")
add_library(pocketcentury SHARED
    jni_bridge.cpp
    ${CORE}/sy909.cpp
    ${UNSP})
target_include_directories(pocketcentury PRIVATE ${CORE} ${CORE}/unsp)
target_compile_options(pocketcentury PRIVATE -O3 -fno-exceptions -Wno-unused-but-set-variable)
target_link_libraries(pocketcentury android jnigraphics log)
