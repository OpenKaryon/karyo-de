find_package(qtermwidget6 REQUIRED)
qt_add_executable(karyo-terminal src/main.cpp src/terminalwindow.cpp src/terminalwindow.h)
target_link_libraries(karyo-terminal PRIVATE Karyo::Style qtermwidget6)
karyo_add_translation(karyo-terminal "${CMAKE_CURRENT_SOURCE_DIR}/translations/karyo-terminal_ru.ts")
install(TARGETS karyo-terminal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT terminal-runtime)
karyo_install_desktop_entry(data/karyo-terminal.desktop terminal-runtime)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-terminal COMPONENT terminal-runtime)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-terminal-link-test tests/linktest.cpp src/terminalwindow.cpp src/terminalwindow.h)
    target_include_directories(karyo-terminal-link-test PRIVATE src)
    target_link_libraries(karyo-terminal-link-test PRIVATE Karyo::Style qtermwidget6 Qt6::Test)
    add_test(NAME terminal-control-click COMMAND karyo-terminal-link-test)
    set_tests_properties(terminal-control-click PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME terminal-command-execution COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/commandtest.py $<TARGET_FILE:karyo-terminal>)
    set_tests_properties(terminal-command-execution PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

configure_file(data/terminal.in "${CMAKE_CURRENT_BINARY_DIR}/terminal" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/terminal" DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT terminal-runtime)
