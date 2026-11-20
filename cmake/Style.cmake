add_library(karyo-style STATIC
    include/karyo/ui/style.h include/karyo/ui/clientframe.h
    src/style.cpp src/clientframe.cpp)
add_library(Karyo::Style ALIAS karyo-style)
set_target_properties(karyo-style PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Style)
target_include_directories(karyo-style PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(karyo-style PUBLIC Karyo::Widgets)
target_compile_options(karyo-style PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

set(style_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/KaryoStyle")
configure_package_config_file(cmake/KaryoStyleConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoStyleConfig.cmake"
    INSTALL_DESTINATION "${style_config_dir}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoStyleConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS karyo-style EXPORT KaryoStyleTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT style-development)
install(FILES include/karyo/ui/style.h include/karyo/ui/clientframe.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/karyo/ui COMPONENT style-development)
install(EXPORT KaryoStyleTargets FILE KaryoStyleTargets.cmake NAMESPACE Karyo::
    DESTINATION "${style_config_dir}" COMPONENT style-development)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/KaryoStyleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/KaryoStyleConfigVersion.cmake"
    DESTINATION "${style_config_dir}" COMPONENT style-development)
install(FILES LICENSE docs/STYLE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-style-dev COMPONENT style-development)

qt_add_plugin(karyo-style-plugin CLASS_NAME KaryoStylePlugin
    plugins/style/plugin.cpp plugins/style/karyo.json)
target_link_libraries(karyo-style-plugin PRIVATE Karyo::Style)
set_target_properties(karyo-style-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/styles")
install(TARGETS karyo-style-plugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/plugins/styles COMPONENT style-runtime)
install(FILES LICENSE docs/STYLE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-style COMPONENT style-runtime)
install(FILES resources/lucide/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-style
    RENAME Lucide-LICENSE COMPONENT style-runtime)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-style-test tests/styletest.cpp)
    target_link_libraries(karyo-style-test PRIVATE Karyo::Style Qt6::Test)
    add_test(NAME qt-style-plugin COMMAND karyo-style-test)
    add_test(NAME qt-client-frames COMMAND karyo-style-test)
    set_tests_properties(qt-style-plugin qt-client-frames PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${CMAKE_CURRENT_BINARY_DIR}/plugins")
    set_property(TEST qt-client-frames APPEND PROPERTY ENVIRONMENT "KARYO_CLIENT_FRAME=1")
endif()
