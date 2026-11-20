if(NOT KARYO_BUILD_SESSION)
    return()
endif()
find_package(LayerShellQt REQUIRED)
find_package(PolkitQt6-1 REQUIRED)
set(shell_dir "${PROJECT_SOURCE_DIR}/shell/src")
add_library(karyo-shell-core STATIC
    ${shell_dir}/buttons.cpp ${shell_dir}/buttons.h
    ${shell_dir}/flyout.cpp ${shell_dir}/flyout.h
    ${shell_dir}/layersurface.cpp ${shell_dir}/layersurface.h
    ${shell_dir}/networkstatus.cpp ${shell_dir}/networkstatus.h
    ${shell_dir}/sessionlocker.cpp ${shell_dir}/sessionlocker.h
    ${shell_dir}/keyboardlayout.cpp ${shell_dir}/keyboardlayout.h
    ${shell_dir}/toplevels.cpp ${shell_dir}/toplevels.h
    ${shell_dir}/fileops.cpp ${shell_dir}/fileops.h
    ${shell_dir}/filetypes.cpp ${shell_dir}/filetypes.h
    ${shell_dir}/wifi.cpp ${shell_dir}/wifi.h)
target_include_directories(karyo-shell-core PUBLIC ${shell_dir} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(karyo-shell-core PUBLIC Karyo::Style Qt6::DBus Qt6::WaylandClient LayerShellQt::Interface)
qt6_generate_wayland_protocol_client_sources(karyo-shell-core FILES
    ${PROJECT_SOURCE_DIR}/shell/protocols/openkaryon-keyboard-layout-v1.xml
    ${PROJECT_SOURCE_DIR}/shell/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)
