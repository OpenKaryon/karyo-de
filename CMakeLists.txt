cmake_minimum_required(VERSION 3.21)
project(KaryoDE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
if(CMAKE_INSTALL_PREFIX STREQUAL "/usr" AND NOT DEFINED CMAKE_INSTALL_SYSCONFDIR)
    set(CMAKE_INSTALL_SYSCONFDIR /etc CACHE PATH "Host configuration directory")
endif()
include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
add_library(karyo-ui-foundation STATIC
    include/karyo/ui/theme.h
    include/karyo/ui/paint.h
    src/theme.cpp
    src/paint.cpp
)
add_library(Karyo::UiFoundation ALIAS karyo-ui-foundation)
set_target_properties(karyo-ui-foundation PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME UiFoundation
)
target_include_directories(karyo-ui-foundation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(karyo-ui-foundation PUBLIC Qt6::Widgets)
target_compile_options(karyo-ui-foundation PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/KaryoUiFoundation")
configure_package_config_file(cmake/KaryoUiFoundationConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoUiFoundationConfig.cmake"
    INSTALL_DESTINATION "${config_dir}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoUiFoundationConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(TARGETS karyo-ui-foundation EXPORT KaryoUiFoundationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
install(FILES include/karyo/ui/theme.h include/karyo/ui/paint.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karyo/ui COMPONENT development)
install(EXPORT KaryoUiFoundationTargets
    FILE KaryoUiFoundationTargets.cmake NAMESPACE Karyo::
    DESTINATION "${config_dir}" COMPONENT development)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoUiFoundationConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoUiFoundationConfigVersion.cmake"
    DESTINATION "${config_dir}" COMPONENT development)
install(FILES LICENSE docs/PROVENANCE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/karyo-ui-foundation-dev"
    COMPONENT development)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-ui-foundation-test tests/foundationtest.cpp)
    target_link_libraries(karyo-ui-foundation-test PRIVATE Karyo::UiFoundation Qt6::Test)
    add_test(NAME ui-foundation COMMAND karyo-ui-foundation-test)
    set_tests_properties(ui-foundation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

option(KARYO_BUILD_DESKTOP_INTEGRATION "Build desktop entry and icon integration" ON)
if(KARYO_BUILD_DESKTOP_INTEGRATION)
    include(cmake/DesktopIntegration.cmake)
    include(cmake/Widgets.cmake)
    include(cmake/Style.cmake)
endif()

include(cmake/Session.cmake)

include(cmake/Lock.cmake)

include(cmake/Shell.cmake)

option(KARYO_BUILD_APPS "Build the independently packaged desktop applications" ${KARYO_BUILD_DESKTOP_INTEGRATION})
if(KARYO_BUILD_APPS)
    if(NOT KARYO_BUILD_DESKTOP_INTEGRATION)
        message(FATAL_ERROR "Karyo applications require desktop integration and widgets")
    endif()
    include(cmake/Translations.cmake)
    add_subdirectory(apps/common)
    add_subdirectory(apps/terminal)
    add_subdirectory(apps/camera)
    add_subdirectory(apps/photos)
    add_subdirectory(apps/calculator)
    add_subdirectory(apps/clock)
    add_subdirectory(apps/editor)
    add_subdirectory(apps/reader)
    add_subdirectory(apps/player)
    add_subdirectory(apps/monitor)
endif()
