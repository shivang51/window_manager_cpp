set(PROJECT_NAME window_manager)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(External
    ${EXT_OTHERS_SRC}
)

set(Header_Files
        "include/window_manager/window_manager.hpp"
)

source_group("include" FILES ${Header_Files})

set(Source_Files
        "src/window_manager.cpp"
        "src/wayland/wayland_window_manager.cpp"
        "src/wayland/wayland_window_manager.hpp"
)

source_group("src" FILES ${Source_Files})

set(ALL_FILES
    ${Header_Files}
    ${Source_Files}
		${EXT_OTHERS_SRC}
)

add_library(${PROJECT_NAME} STATIC ${ALL_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)

target_include_directories(${PROJECT_NAME} PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG>)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "/utf-8" "/Zc:__cplusplus")
endif()

# Wayland and protocol generation (Linux)
if(UNIX)
    target_link_libraries(${PROJECT_NAME} PUBLIC wayland-client)

    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
    set(WAYLAND_PROTOCOLS_DIR "/usr/share/wayland-protocols" CACHE PATH "Path to wayland-protocols share directory")
    set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

    if(NOT EXISTS "${XDG_SHELL_XML}")
        message(FATAL_ERROR "Could not find xdg-shell.xml at ${XDG_SHELL_XML}. Install wayland-protocols or set WAYLAND_PROTOCOLS_DIR.")
    endif()
    if(NOT WAYLAND_SCANNER_EXECUTABLE)
        message(FATAL_ERROR "wayland-scanner not found. Install wayland providing wayland-scanner.")
    endif()

    set(GENERATED_PROTOCOL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-client-protocol.h")
    set(GENERATED_PROTOCOL_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

    add_custom_command(
        OUTPUT "${GENERATED_PROTOCOL_HEADER}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${XDG_SHELL_XML}" "${GENERATED_PROTOCOL_HEADER}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-client-protocol.h"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${GENERATED_PROTOCOL_SOURCE}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${XDG_SHELL_XML}" "${GENERATED_PROTOCOL_SOURCE}"
        DEPENDS "${XDG_SHELL_XML}"
        COMMENT "Generating xdg-shell-protocol.c"
        VERBATIM
    )
    add_custom_target(window_manager_wayland_protocol DEPENDS "${GENERATED_PROTOCOL_HEADER}" "${GENERATED_PROTOCOL_SOURCE}")
    add_dependencies(${PROJECT_NAME} window_manager_wayland_protocol)
    target_sources(${PROJECT_NAME} PRIVATE "${GENERATED_PROTOCOL_SOURCE}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
endif()

