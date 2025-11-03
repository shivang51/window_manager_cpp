set(PROJECT_NAME WindowManagerTest)
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_BUILD_RPATH "\$ORIGIN")
set(CMAKE_INSTALL_RPATH "\$ORIGIN")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(External
)

set(Header_Files
)

source_group("include" FILES ${Header_Files})

set(Source_Files
		"main.cpp"
)

source_group("src" FILES ${Source_Files})

set(ALL_FILES
    ${External}
    ${Header_Files}
    ${Source_Files}
)

add_executable(${PROJECT_NAME} ${ALL_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE window_manager wayland-client)
if(DEFINED WM_USE_VULKAN AND WM_USE_VULKAN)
    message("[WindowManager] Vulkan support enabled")
    if(TARGET Vulkan::Vulkan)
        target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
    else()
        find_package(Vulkan)
        if(Vulkan_FOUND)
            target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
        endif()
    endif()
endif()
add_dependencies(${PROJECT_NAME} window_manager)

target_include_directories(${PROJECT_NAME} PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/window_manager/include"
)

