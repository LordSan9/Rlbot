cmake_minimum_required(VERSION 3.20)
project(RLBot_Win VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies paths (edit these to your local installs) ──────────────────
# ONNX Runtime: https://github.com/microsoft/onnxruntime/releases
# Download onnxruntime-win-x64-*.zip, extract next to this repo
set(ONNXRUNTIME_DIR "${CMAKE_SOURCE_DIR}/../onnxruntime-win-x64-1.19.2"
    CACHE PATH "Path to ONNX Runtime root")

# ViGEmClient: https://github.com/nefarius/ViGEmClient
# Clone and build, or use the NuGet package
set(VIGEMCLIENT_DIR "${CMAKE_SOURCE_DIR}/../ViGEmClient"
    CACHE PATH "Path to ViGEmClient root")

# ── Compiler flags ───────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /O2 /MP /Zi)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00   # Windows 10+
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/process_reader.cpp
    src/offset_resolver.cpp
    src/game_state.cpp
    src/bot_controller.cpp
    src/nexto_inference.cpp
    src/vigem_input.cpp
    src/experience_recorder.cpp
    src/overlay.cpp
)

set(HEADERS
    include/process_reader.h
    include/offset_resolver.h
    include/game_state.h
    include/bot_controller.h
    include/nexto_inference.h
    include/vigem_input.h
    include/experience_recorder.h
    include/overlay.h
    include/rl_structs.h
    include/config.h
)

add_executable(rlbot_win ${SOURCES} ${HEADERS})

# ── Include dirs ─────────────────────────────────────────────────────────────
target_include_directories(rlbot_win PRIVATE
    include
    "${ONNXRUNTIME_DIR}/include"
    "${VIGEMCLIENT_DIR}/include"
)

# ── ONNX Runtime ─────────────────────────────────────────────────────────────
target_link_libraries(rlbot_win PRIVATE
    "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib"
)

# Copy onnxruntime.dll next to the exe
add_custom_command(TARGET rlbot_win POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:rlbot_win>
)

# ── ViGEm ────────────────────────────────────────────────────────────────────
target_link_libraries(rlbot_win PRIVATE
    "${VIGEMCLIENT_DIR}/lib/release/x64/ViGEmClient.lib"
)

add_custom_command(TARGET rlbot_win POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VIGEMCLIENT_DIR}/lib/release/x64/ViGEmClient.dll"
        $<TARGET_FILE_DIR:rlbot_win>
)

# ── Windows system libs ───────────────────────────────────────────────────────
target_link_libraries(rlbot_win PRIVATE
    psapi.lib
    ntdll.lib
)

# ── Debug symbols ─────────────────────────────────────────────────────────────
set_target_properties(rlbot_win PROPERTIES
    PDB_NAME "rlbot_win"
    COMPILE_PDB_NAME "rlbot_win"
)
