option(KARYO_BUILD_SESSION "Build Wayland shell and lock components" ON)
if(NOT KARYO_BUILD_SESSION)
    return()
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS DBus Network WaylandClient)
include(${CMAKE_CURRENT_LIST_DIR}/Translations.cmake)
find_library(PAM_LIBRARY pam REQUIRED)
qt_add_executable(karyo-lock
    ${PROJECT_SOURCE_DIR}/lock/main.cpp
    ${PROJECT_SOURCE_DIR}/lock/signin.cpp
    ${PROJECT_SOURCE_DIR}/lock/signinview.cpp
    ${PROJECT_SOURCE_DIR}/lock/signinview.h
    ${PROJECT_SOURCE_DIR}/lock/lock/lockmain.cpp
    ${PROJECT_SOURCE_DIR}/lock/lock/pamcheck.cpp
    ${PROJECT_SOURCE_DIR}/lock/lock/pamcheck.h
    ${PROJECT_SOURCE_DIR}/lock/lock/sessionlock.cpp
    ${PROJECT_SOURCE_DIR}/lock/lock/sessionlock.h)
target_include_directories(karyo-lock PRIVATE ${PROJECT_SOURCE_DIR}/lock)
target_link_libraries(karyo-lock PRIVATE Karyo::Style Qt6::DBus Qt6::WaylandClient
    Qt6::WaylandClientPrivate ${PAM_LIBRARY})
qt6_generate_wayland_protocol_client_sources(karyo-lock
    FILES ${PROJECT_SOURCE_DIR}/lock/protocols/ext-session-lock-v1.xml)
install(TARGETS karyo-lock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT lock-runtime)
install(FILES ${PROJECT_SOURCE_DIR}/lock/pam.d/karyo-lock
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/pam.d COMPONENT lock-runtime)
install(FILES LICENSE docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-lock COMPONENT lock-runtime)

if(BUILD_TESTING)
    add_executable(karyo-pam-test tests/pamtest.cpp lock/lock/pamcheck.cpp lock/lock/pamcheck.h)
    target_include_directories(karyo-pam-test PRIVATE lock/lock)
    target_link_libraries(karyo-pam-test PRIVATE Qt6::Test)
    add_test(NAME lock-pam-policy COMMAND karyo-pam-test)
endif()

install(FILES lock/protocols/ext-session-lock-v1.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-lock/protocols COMPONENT lock-runtime)

karyo_add_translation(karyo-lock "${PROJECT_SOURCE_DIR}/lock/translations/karyo-lock_ru.ts")

if(BUILD_TESTING)
    add_executable(karyo-signin-view-test tests/signinviewtest.cpp lock/signinview.cpp lock/signinview.h)
    target_include_directories(karyo-signin-view-test PRIVATE lock)
    target_link_libraries(karyo-signin-view-test PRIVATE Karyo::Widgets Qt6::Test)
    add_test(NAME lock-error-layout COMMAND karyo-signin-view-test)
    set_tests_properties(lock-error-layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
