configure_file(session/karyo-session.in "${CMAKE_CURRENT_BINARY_DIR}/karyo-session" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-session"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT session-runtime)
install(FILES session/keyboard session/renderer
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/session COMPONENT session-runtime)
install(FILES session/karyo.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions COMPONENT session-runtime)
install(FILES session/labwc/rc.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/session/labwc COMPONENT session-runtime)
install(PROGRAMS session/labwc/autostart
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/session/labwc COMPONENT session-runtime)
install(FILES LICENSE docs/SESSION.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-session COMPONENT session-runtime)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME session-launch-contract COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sessiontest.py)
endif()

install(FILES session/karyo-portals.conf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal COMPONENT session-runtime)
install(FILES LICENSE docs/SESSION.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-session-bundle COMPONENT session-bundle)
