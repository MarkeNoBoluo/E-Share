cmake_minimum_required(VERSION 3.12)
project(E-Share LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Architecture ────────────────────────────────────────────────────────
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_ARCH x64)
else()
    set(TARGET_ARCH x86)
endif()
message(STATUS "Target architecture: ${TARGET_ARCH}")

# ── Dependencies via FetchContent ───────────────────────────────────────
include(FetchContent)

set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")

# nlohmann/json
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
    add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR})
endif()

# cpp-httplib
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
)
FetchContent_GetProperties(httplib)
if(NOT httplib_POPULATED)
    FetchContent_Populate(httplib)
    add_subdirectory(${httplib_SOURCE_DIR} ${httplib_BINARY_DIR})
endif()

# ── Sources ─────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/network/NetworkUtil.h
    src/config/ConfigLoader.cpp
    src/common/Logger.cpp
    src/control/DeviceDiscovery.cpp
    src/control/ControlServer.cpp
    src/control/ControlClient.cpp
    src/media/RtspServerManager.cpp
    src/media/PushService.cpp
    src/media/PullService.cpp
    src/session/SessionManager.cpp
    src/cli/CliHandler.cpp
)

# ── Executable ──────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4996)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    httplib::httplib
    ws2_32
    ole32  # CoCreateGuid
)

# ── Binary directory ────────────────────────────────────────────────────
set(BIN_DIR "${CMAKE_SOURCE_DIR}/bin")

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIN_DIR}"
)

# ── Copy RTSP-Pusher.exe + DLLs ────────────────────────────────────────
set(PUSHER_SRC "${CMAKE_SOURCE_DIR}/../RTSP-Pusher/bin/MSVC2017_${TARGET_ARCH}_Release")
# Fallback to the absolute reference project path
if(NOT EXISTS "${PUSHER_SRC}/RTSP-Pusher.exe")
    set(PUSHER_SRC "D:/Repositories/QT-Resources/RTSP-Pusher/bin/MSVC2017_${TARGET_ARCH}_Release")
endif()

file(MAKE_DIRECTORY "${BIN_DIR}")

# Copy RTSP-Pusher binaries at configure time (one-time)
if(EXISTS "${PUSHER_SRC}/RTSP-Pusher.exe")
    file(GLOB PUSHER_FILES "${PUSHER_SRC}/*")
    foreach(f ${PUSHER_FILES})
        get_filename_component(fname "${f}" NAME)
        configure_file("${f}" "${BIN_DIR}/${fname}" COPYONLY)
    endforeach()
    message(STATUS "Copied RTSP-Pusher binaries to ${BIN_DIR}")
else()
    message(WARNING "RTSP-Pusher binaries not found at ${PUSHER_SRC}")
endif()

# ── Copy RTSP-Player.exe + DLLs ────────────────────────────────────────
set(PLAYER_SRC "${CMAKE_SOURCE_DIR}/../RTSP-Player/bin/MSVC2017_${TARGET_ARCH}_Release")
if(NOT EXISTS "${PLAYER_SRC}/RTSP-Player.exe")
    set(PLAYER_SRC "D:/Repositories/QT-Resources/RTSP-Player/bin/MSVC2017_${TARGET_ARCH}_Release")
endif()

if(EXISTS "${PLAYER_SRC}/RTSP-Player.exe")
    file(GLOB PLAYER_FILES "${PLAYER_SRC}/*")
    foreach(f ${PLAYER_FILES})
        get_filename_component(fname "${f}" NAME)
        configure_file("${f}" "${BIN_DIR}/${fname}" COPYONLY)
    endforeach()
    message(STATUS "Copied RTSP-Player binaries to ${BIN_DIR}")
else()
    message(WARNING "RTSP-Player binaries not found at ${PLAYER_SRC}")
endif()

# ── Download MediaMTX ──────────────────────────────────────────────────
if(TARGET_ARCH STREQUAL "x64")
    set(MEDIAMTX_URL "https://github.com/bluenviron/mediamtx/releases/download/v1.19.2/mediamtx_v1.19.2_windows_amd64.zip")
    set(MEDIAMTX_ARCHIVE "${CMAKE_BINARY_DIR}/mediamtx.zip")
    set(MEDIAMTX_EXE "${BIN_DIR}/mediamtx.exe")

    if(NOT EXISTS "${MEDIAMTX_EXE}")
        message(STATUS "Downloading MediaMTX...")
        file(DOWNLOAD "${MEDIAMTX_URL}" "${MEDIAMTX_ARCHIVE}"
            STATUS downloadStatus
            TIMEOUT 120)

        list(GET downloadStatus 0 statusCode)
        if(statusCode EQUAL 0)
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${MEDIAMTX_ARCHIVE}"
                WORKING_DIRECTORY "${BIN_DIR}"
            )
            message(STATUS "MediaMTX downloaded to ${BIN_DIR}")
        else()
            message(WARNING "Failed to download MediaMTX: ${downloadStatus}")
        endif()
    else()
        message(STATUS "MediaMTX already present: ${MEDIAMTX_EXE}")
    endif()
else()
    message(WARNING "MediaMTX 无 32 位 Windows 构建，跳过下载。请手动将 mediamtx.exe 放入 ${BIN_DIR}")
endif()
