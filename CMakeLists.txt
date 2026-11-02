cmake_minimum_required(VERSION 3.27)

project(hyprion
    DESCRIPTION "Hyprion visual effects for Hyprland"
    VERSION 0.4.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)

find_program(HYPRWAYLAND_SCANNER hyprwayland-scanner REQUIRED)
set(HYPRION_PROTOCOL_OUT "${CMAKE_CURRENT_BINARY_DIR}/protocols")
add_custom_command(
    OUTPUT
        "${HYPRION_PROTOCOL_OUT}/hyprion-grade-v1.hpp"
        "${HYPRION_PROTOCOL_OUT}/hyprion-grade-v1.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${HYPRION_PROTOCOL_OUT}"
    COMMAND "${HYPRWAYLAND_SCANNER}" "${CMAKE_CURRENT_SOURCE_DIR}/protocols/hyprion-grade-v1.xml" "${HYPRION_PROTOCOL_OUT}/"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/protocols/hyprion-grade-v1.xml"
    VERBATIM
)

add_library(hyprion SHARED
    src/main.cpp
    src/HyprionConfig.cpp
    src/HyprionColor.cpp
    src/HyprionDecoration.cpp
    src/HyprionGradeRenderer.cpp
    src/HyprionHooks.cpp
    src/HyprionSnapshotCache.cpp
    src/HyprionGradeProtocol.cpp
    "${HYPRION_PROTOCOL_OUT}/hyprion-grade-v1.cpp"
)

target_include_directories(hyprion PRIVATE "${HYPRION_PROTOCOL_OUT}")

target_link_libraries(hyprion PRIVATE rt PkgConfig::deps)
