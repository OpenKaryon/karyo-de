add_library(karyo-widgets STATIC
    include/karyo/ui/window.h
    include/karyo/ui/navigation.h
    include/karyo/ui/password.h
    include/karyo/ui/menu.h
    include/karyo/ui/tooltip.h
    src/window.cpp src/frame.h src/navigation.cpp src/password.cpp src/menu.cpp src/tooltip.cpp)
add_library(Karyo::Widgets ALIAS karyo-widgets)
set_target_properties(karyo-widgets PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Widgets)
target_include_directories(karyo-widgets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(karyo-widgets PUBLIC Karyo::DesktopIntegration)
target_compile_options(karyo-widgets PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

set(widget_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/KaryoWidgets")
configure_package_config_file(cmake/KaryoWidgetsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoWidgetsConfig.cmake"
    INSTALL_DESTINATION "${widget_config_dir}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoWidgetsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS karyo-widgets EXPORT KaryoWidgetsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT widgets-development)
install(FILES include/karyo/ui/window.h include/karyo/ui/navigation.h
    include/karyo/ui/password.h include/karyo/ui/menu.h include/karyo/ui/tooltip.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karyo/ui COMPONENT widgets-development)
install(EXPORT KaryoWidgetsTargets FILE KaryoWidgetsTargets.cmake NAMESPACE Karyo::
    DESTINATION "${widget_config_dir}" COMPONENT widgets-development)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoWidgetsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoWidgetsConfigVersion.cmake"
    DESTINATION "${widget_config_dir}" COMPONENT widgets-development)
install(FILES LICENSE docs/WIDGETS.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-widgets-dev COMPONENT widgets-development)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-widgets-test tests/widgettest.cpp)
    target_link_libraries(karyo-widgets-test PRIVATE Karyo::Widgets Qt6::Test)
    add_test(NAME shared-widgets COMMAND karyo-widgets-test)
    set_tests_properties(shared-widgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
