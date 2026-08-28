cmake_minimum_required(VERSION 3.15)
project(rakhus VERSION 1.0.0 LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(DX_LIBS d3d11 dxgi d3dcompiler)
set(WIN_LIBS winmm ws2_32)

# Források automatikus gyűjtése
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")
if(NOT SOURCES)
    file(GLOB_RECURSE SOURCES "*.cpp" "*.c")
endif()

file(GLOB_RECURSE HEADERS "src/*.h" "src/*.hpp")

set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kiero
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kiero/minhook/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kiero/minhook/src
)

# RC fájl NÉLKÜL - csak SOURCES és HEADERS
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${INCLUDE_DIRS})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
    IMGUI_STB_TRUETYPE_FILENAME="imstb_truetype.h"
    STB_RECT_PACK_FILENAME="imstb_rectpack.h"
    UNICODE
    _UNICODE
)

# FIX: Remove /EHsc and add /EHa for SEH + C++ exceptions
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    
    target_compile_options(${PROJECT_NAME} PRIVATE /EHa)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${DX_LIBS} ${WIN_LIBS})

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)