cmake_minimum_required(VERSION 3.20)
project(AssassinsCreedUnityHeadTracking VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- Dependencies ---

# MinHook for camera function hooking
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(minhook)

file(GLOB MINHOOK_SOURCES
    ${minhook_SOURCE_DIR}/src/*.c
    ${minhook_SOURCE_DIR}/src/hde/*.c
)
add_library(minhook STATIC ${MINHOOK_SOURCES})
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# cameraunlock-core shared library
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# --- Mod sources ---

set(MOD_SOURCES
    src/pch.cpp
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/logger.cpp
    src/core/config.cpp
    src/core/path_utils.cpp
    src/hooks/hook_manager.cpp
    src/hooks/input_hook.cpp
    src/hooks/camera_hook.cpp
)

add_library(AssassinsCreedUnityHeadTracking SHARED ${MOD_SOURCES})

set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_precompile_headers(AssassinsCreedUnityHeadTracking PRIVATE src/pch.h)

# Output as .asi for Ultimate ASI Loader
set_target_properties(AssassinsCreedUnityHeadTracking PROPERTIES
    OUTPUT_NAME "AssassinsCreedUnityHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(AssassinsCreedUnityHeadTracking PRIVATE
    cameraunlock
    minhook
)

target_include_directories(AssassinsCreedUnityHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Temporary RE scaffolding: arms the in-memory actor-cull-frustum locator
# (Ctrl+Shift+U). Debug config only - never compiled into a release .asi.
target_compile_definitions(AssassinsCreedUnityHeadTracking PRIVATE
    $<$<CONFIG:Debug>:ACUHT_DEBUG_FRUSTUM_SCAN=1>)

if(WIN32)
    target_link_libraries(AssassinsCreedUnityHeadTracking PRIVATE
        ws2_32
        Psapi
    )
endif()

if(MSVC)
    # /wd4324: suppress "structure padded due to alignment specifier" warnings
    # from cameraunlock-core's atomic-aligned receiver state.
    target_compile_options(AssassinsCreedUnityHeadTracking PRIVATE /W4 /MP /EHa /wd4324)
endif()
