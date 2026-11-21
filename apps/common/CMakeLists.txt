find_package(Qt6 REQUIRED COMPONENTS DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(KARYO_FILEMANAGER_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
find_program(KARYO_FILEMANAGER_SCANNER wayland-scanner REQUIRED)
set(activation_xml "${CMAKE_CURRENT_SOURCE_DIR}/protocols/xdg-activation-v1.xml")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.h"
    COMMAND ${KARYO_FILEMANAGER_SCANNER} client-header ${activation_xml}
        "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.h"
    DEPENDS ${activation_xml} VERBATIM)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.c"
    COMMAND ${KARYO_FILEMANAGER_SCANNER} private-code ${activation_xml}
        "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.c"
    DEPENDS ${activation_xml} VERBATIM)
add_library(karyo-app-filemanager STATIC filemanager.cpp filemanager.h
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.h"
    "${CMAKE_CURRENT_BINARY_DIR}/xdg-activation-client.c")
target_include_directories(karyo-app-filemanager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(karyo-app-filemanager PUBLIC Qt6::Gui Qt6::DBus PRIVATE Qt6::GuiPrivate PkgConfig::KARYO_FILEMANAGER_WAYLAND)
# Each independent runtime keeps the generated MIT protocol's original notice.
foreach(app IN ITEMS archive photos monitor appcenter reader snip player settings)
    install(FILES ${activation_xml} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-${app}/protocols
        COMPONENT ${app}-runtime)
endforeach()
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-app-filemanager-fixture filemanagerfixture.cpp)
    target_link_libraries(karyo-app-filemanager-fixture PRIVATE karyo-app-filemanager Qt6::Widgets)
    add_executable(karyo-app-filemanager-test filemanagertest.cpp)
    target_link_libraries(karyo-app-filemanager-test PRIVATE karyo-app-filemanager Qt6::Test Qt6::Widgets)
    add_test(NAME app-filemanager COMMAND dbus-run-session -- $<TARGET_FILE:karyo-app-filemanager-test>)
    set_tests_properties(app-filemanager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_library(karyo-app-command STATIC command.cpp command.h)
target_include_directories(karyo-app-command PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(karyo-app-command PUBLIC Qt6::Core)
if(BUILD_TESTING)
    add_executable(karyo-app-command-test commandtest.cpp)
    target_link_libraries(karyo-app-command-test PRIVATE karyo-app-command Qt6::Test)
    add_test(NAME app-bounded-command COMMAND karyo-app-command-test)
    set_tests_properties(app-bounded-command PROPERTIES TIMEOUT 45)
endif()

set(KARYO_APT_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/karyo/appcenter-apt")
configure_file(software/org.karyo.software.policy.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.karyo.software.policy @ONLY)
add_library(karyo-app-software STATIC softwarejob.cpp softwarejob.h)
target_include_directories(karyo-app-software PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(karyo-app-software PUBLIC Qt6::Core)
target_compile_definitions(karyo-app-software PRIVATE KARYO_APT_HELPER_PATH="${KARYO_APT_HELPER_PATH}")
install(PROGRAMS software/apt_helper.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo
    RENAME appcenter-apt COMPONENT software-runtime)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.karyo.software.policy
    DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions COMPONENT software-runtime)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-software COMPONENT software-runtime)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(karyo-app-software-test softwarejobtest.cpp)
    target_link_libraries(karyo-app-software-test PRIVATE karyo-app-software Qt6::Widgets Qt6::Test)
    add_test(NAME app-software-jobs COMMAND karyo-app-software-test)
    set_tests_properties(app-software-jobs PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
    add_test(NAME apt-helper-request-boundary COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/software/test_apt_helper.py)
endif()
