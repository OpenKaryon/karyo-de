find_package(Qt6 6.4 REQUIRED COMPONENTS Svg)
qt_add_resources(lucide_sources resources/karyo_lucide.qrc)
add_library(karyo-desktop-integration STATIC
    include/karyo/ui/appstyle.h
    include/karyo/ui/desktopentry.h
    include/karyo/ui/icons.h
    src/appstyle.cpp
    src/desktopentry.cpp
    src/icons.cpp
    ${lucide_sources}
)
add_library(Karyo::DesktopIntegration ALIAS karyo-desktop-integration)
set_target_properties(karyo-desktop-integration PROPERTIES
    POSITION_INDEPENDENT_CODE ON EXPORT_NAME DesktopIntegration)
target_include_directories(karyo-desktop-integration PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(karyo-desktop-integration PUBLIC Karyo::UiFoundation Qt6::Svg)
target_compile_options(karyo-desktop-integration PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

set(integration_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/KaryoDesktopIntegration")
configure_package_config_file(cmake/KaryoDesktopIntegrationConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoDesktopIntegrationConfig.cmake"
    INSTALL_DESTINATION "${integration_config_dir}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoDesktopIntegrationConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS karyo-desktop-integration EXPORT KaryoDesktopIntegrationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT desktop-development)
install(FILES
    include/karyo/ui/appstyle.h include/karyo/ui/desktopentry.h include/karyo/ui/icons.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karyo/ui COMPONENT desktop-development)
install(EXPORT KaryoDesktopIntegrationTargets
    FILE KaryoDesktopIntegrationTargets.cmake NAMESPACE Karyo::
    DESTINATION "${integration_config_dir}" COMPONENT desktop-development)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoDesktopIntegrationConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoDesktopIntegrationConfigVersion.cmake"
    DESTINATION "${integration_config_dir}" COMPONENT desktop-development)
install(FILES resources/lucide/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-desktop-integration-dev
    RENAME Lucide-LICENSE COMPONENT desktop-development)
install(FILES LICENSE docs/DESKTOP_INTEGRATION.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-desktop-integration-dev
    COMPONENT desktop-development)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-desktop-entry-test tests/desktopentrytest.cpp)
    target_link_libraries(karyo-desktop-entry-test PRIVATE Karyo::DesktopIntegration Qt6::Test)
    add_test(NAME desktop-entry-icons COMMAND karyo-desktop-entry-test)
    set_tests_properties(desktop-entry-icons PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
