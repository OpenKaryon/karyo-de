if(TARGET karyo-toplevels)
    return()
endif()
# Internal Wayland window observation for shell and application-launch feedback.
find_package(Qt6 6.4 REQUIRED COMPONENTS WaylandClient)
add_library(karyo-toplevels STATIC
    ${PROJECT_SOURCE_DIR}/src/wayland/toplevels.cpp ${PROJECT_SOURCE_DIR}/src/wayland/toplevels.h)
target_include_directories(karyo-toplevels PUBLIC
    ${PROJECT_SOURCE_DIR}/src/wayland ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(karyo-toplevels PUBLIC Qt6::Gui Qt6::WaylandClient)
qt6_generate_wayland_protocol_client_sources(karyo-toplevels FILES
    ${PROJECT_SOURCE_DIR}/shell/protocols/wlr-foreign-toplevel-management-unstable-v1.xml)
