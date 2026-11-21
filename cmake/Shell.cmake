if(NOT KARYO_BUILD_SESSION)
    return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/DesktopAccounts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Toplevels.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Wifi.cmake)
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
)
target_include_directories(karyo-shell-core PUBLIC ${shell_dir} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(karyo-shell-core PUBLIC karyo-desktop-accounts Karyo::Style karyo-file-presentation karyo-file-operations karyo-toplevels karyo-wifi Qt6::DBus Qt6::WaylandClient LayerShellQt::Interface)
qt6_generate_wayland_protocol_client_sources(karyo-shell-core FILES
    ${PROJECT_SOURCE_DIR}/shell/protocols/openkaryon-keyboard-layout-v1.xml)

add_library(karyo-shell-tray STATIC
    ${shell_dir}/tray/dbusmenu.cpp ${shell_dir}/tray/dbusmenu.h
    ${shell_dir}/tray/statusnotifierwatcher.cpp ${shell_dir}/tray/statusnotifierwatcher.h
    ${shell_dir}/tray/trayhost.cpp ${shell_dir}/tray/trayhost.h
    ${shell_dir}/tray/trayitem.cpp ${shell_dir}/tray/trayitem.h
    ${shell_dir}/trayflyout.cpp ${shell_dir}/trayflyout.h)
target_link_libraries(karyo-shell-tray PUBLIC karyo-shell-core)
if(BUILD_TESTING)
    add_executable(karyo-tray-test tests/traymenutest.cpp)
    target_link_libraries(karyo-tray-test PRIVATE karyo-shell-tray)
    add_test(NAME tray-context-menu COMMAND dbus-run-session -- $<TARGET_FILE:karyo-tray-test>)
    set_tests_properties(tray-context-menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_library(karyo-shell-services STATIC
    ${shell_dir}/notify/notificationbanner.cpp ${shell_dir}/notify/notificationbanner.h
    ${shell_dir}/notify/notificationcard.cpp ${shell_dir}/notify/notificationcard.h
    ${shell_dir}/notify/notificationcenter.cpp ${shell_dir}/notify/notificationcenter.h
    ${shell_dir}/notify/notificationserver.cpp ${shell_dir}/notify/notificationserver.h
    ${shell_dir}/polkit/authdialog.cpp ${shell_dir}/polkit/authdialog.h
    ${shell_dir}/polkit/polkitagent.cpp ${shell_dir}/polkit/polkitagent.h)
target_link_libraries(karyo-shell-services PUBLIC karyo-shell-core PolkitQt6-1::Agent)

add_library(karyo-shell-start STATIC
    ${shell_dir}/start/appindex.cpp ${shell_dir}/start/appindex.h
    ${shell_dir}/start/symbols.cpp ${shell_dir}/start/symbols.h)
target_link_libraries(karyo-shell-start PUBLIC karyo-shell-core)

target_sources(karyo-shell-start PRIVATE
    ${shell_dir}/start/startwidgets.cpp ${shell_dir}/start/startwidgets.h)

target_sources(karyo-shell-start PRIVATE
    ${shell_dir}/start/startmenu.cpp ${shell_dir}/start/startmenu.h)
if(BUILD_TESTING)
    add_executable(karyo-start-test tests/startfiltertest.cpp)
    target_link_libraries(karyo-start-test PRIVATE karyo-shell-start Qt6::Test)
    add_test(NAME start-search-filters COMMAND karyo-start-test)
    set_tests_properties(start-search-filters PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_library(karyo-shell-quick STATIC
    ${shell_dir}/quick/quicksettings.cpp ${shell_dir}/quick/quicksettings.h
    ${shell_dir}/quick/systemcontrols.cpp ${shell_dir}/quick/systemcontrols.h)
target_link_libraries(karyo-shell-quick PUBLIC karyo-shell-core karyo-shell-start karyo-shell-services)

add_library(karyo-shell-desktop STATIC
    ${shell_dir}/desktopicons.cpp ${shell_dir}/desktopicons.h
    ${shell_dir}/wallpaper.cpp ${shell_dir}/wallpaper.h)
target_link_libraries(karyo-shell-desktop PUBLIC karyo-shell-core)

add_library(karyo-appearance-publisher STATIC
    ${shell_dir}/appearancepublisher.cpp ${shell_dir}/appearancepublisher.h)
target_include_directories(karyo-appearance-publisher PUBLIC ${shell_dir})
target_link_libraries(karyo-appearance-publisher PUBLIC Qt6::Core)
if(BUILD_TESTING)
    add_executable(karyo-appearance-publisher-test tests/appearancepublishertest.cpp)
    target_link_libraries(karyo-appearance-publisher-test PRIVATE karyo-appearance-publisher Qt6::Test)
    if(KARYO_BUILD_GREETER)
        target_link_libraries(karyo-appearance-publisher-test PRIVATE karyo-greeter-appearance)
        target_compile_definitions(karyo-appearance-publisher-test PRIVATE KARYO_GREETER_INTEROP)
    endif()
    add_test(NAME appearance-publication COMMAND karyo-appearance-publisher-test)
    set_tests_properties(appearance-publication PROPERTIES TIMEOUT 30)
endif()

qt_add_executable(karyo-shell
    ${shell_dir}/main.cpp ${shell_dir}/taskbar.cpp ${shell_dir}/taskbar.h
    ${shell_dir}/shellservice.cpp ${shell_dir}/shellservice.h)
target_link_libraries(karyo-shell PRIVATE karyo-appearance-publisher karyo-shell-core karyo-shell-start
    karyo-shell-tray karyo-shell-services karyo-shell-quick karyo-shell-desktop)
install(TARGETS karyo-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT shell-runtime)
install(FILES LICENSE docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-shell COMPONENT shell-runtime)

install(FILES shell/protocols/openkaryon-keyboard-layout-v1.xml
    shell/protocols/wlr-foreign-toplevel-management-unstable-v1.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-shell/protocols COMPONENT shell-runtime)

karyo_add_translation(karyo-shell "${PROJECT_SOURCE_DIR}/shell/translations/karyo-shell_ru.ts")

if(BUILD_TESTING)
    add_executable(karyo-shell-services-test tests/shellservicetest.cpp)
    target_link_libraries(karyo-shell-services-test PRIVATE karyo-shell-services Qt6::Test)
    add_test(NAME shell-services-policy COMMAND karyo-shell-services-test)
    set_tests_properties(shell-services-policy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Qt 6.10's portal registry resolves service identities through desktop files.
install(FILES shell/data/karyo-shell.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT shell-runtime)
