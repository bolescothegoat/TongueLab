cmake_minimum_required(VERSION 3.21)
project(TongueLab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG 15.5.0 (alandtse fork, GPL-3.0-or-later with the Modding Exception): reads the AE 1.7 (format 5) Address Library and SE 1.5.97, so one DLL runs on all of them.
# Both paths can be overridden on the cmake command line (see BUILDING.md).
set(COMMONLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../_libs/CommonLibSSE-NG-15.5.0" CACHE PATH "CommonLibSSE-NG 15.5.0 source")
# the packages CommonLibSSE-NG 15.5.0 and TongueLab need (spdlog, fmt, tomlplusplus, rapidcsv, directxtk, xbyak ...), built for the x64-windows-static-md triplet
set(DEPS_ROOT "${CMAKE_SOURCE_DIR}/deps15/x64-windows-static-md" CACHE PATH "prebuilt packages for x64-windows-static-md")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_PREFIX_PATH "${DEPS_ROOT}")
add_subdirectory("${COMMONLIB_DIR}" "${CMAKE_BINARY_DIR}/commonlib" EXCLUDE_FROM_ALL)

add_library(TongueLab SHARED src/main.cpp src/Config.cpp src/NpcCore.cpp src/Npc.cpp src/Tongue.cpp src/UI.cpp)
target_include_directories(TongueLab PRIVATE src)
target_precompile_headers(TongueLab PRIVATE src/PCH.h)
target_link_libraries(TongueLab PRIVATE CommonLibSSE::CommonLibSSE)
target_compile_definitions(TongueLab PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(TongueLab PRIVATE /permissive- /Zc:preprocessor /utf-8 /W4 /wd4100)

find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(TongueLab PRIVATE spdlog::spdlog tomlplusplus::tomlplusplus)

set_target_properties(TongueLab PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/SKSE/Plugins"
)
