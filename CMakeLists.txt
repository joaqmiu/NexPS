cmake_minimum_required(VERSION 3.15)

# Paths
set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library)
set(BOREALIS_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/resources)
set(PROJECT_RESOURCES_BUILD ${CMAKE_BINARY_DIR}/resources)

# Force Switch + deko3d before Borealis options register
set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(USE_DEKO3D ON CACHE BOOL "" FORCE)

# Borealis option machinery
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

# Borealis toolchain (pulls in devkitPro Switch.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# Project
project(NexPS LANGUAGES C CXX)
set(VERSION_MAJOR "2")
set(VERSION_MINOR "0")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "joaqmiu / HayatoG")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)

# Build Borealis library
add_subdirectory(${BOREALIS_LIBRARY} ${CMAKE_BINARY_DIR}/borealis EXCLUDE_FROM_ALL)

find_package(Threads REQUIRED)

# Backend C sources (legacy main.c excluded — replaced by main.cpp)
file(GLOB NEXPS_BACKEND_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.c)
list(FILTER NEXPS_BACKEND_SRC EXCLUDE REGEX "main\\.c$")

set(MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp
    ${NEXPS_BACKEND_SRC}
    ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c
)

set(APP_PLATFORM_LIB
    glfw3 EGL glapi drm_nouveau nx m
    curl mbedtls mbedx509 mbedcrypto z
    archive lzma zstd lz4 bz2 expat
    ${CMAKE_THREAD_LIBS_INIT}
)

add_executable(${PROJECT_NAME} ${MAIN_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 C_STANDARD 11)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB})

# deko3d shader compilation (glsl -> dksh) into build romfs
gen_dksh("${PROJECT_RESOURCES_BUILD}/shaders")

# Final NRO bundling: nacp + romfs (borealis defaults + our shaders) + icon
add_custom_target(${PROJECT_NAME}.nro ALL DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE} --create
        "NexPS"
        "${PROJECT_AUTHOR}"
        "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
        ${PROJECT_NAME}.nacp
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES} ${PROJECT_RESOURCES_BUILD}
    COMMAND ${NX_ELF2NRO_EXE}
        ${PROJECT_NAME}.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${PROJECT_RESOURCES_BUILD}
)
