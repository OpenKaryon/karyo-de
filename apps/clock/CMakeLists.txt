find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)

qt_add_executable(karyo-clock
    src/alarm.cpp
    src/alarm.h
    src/clockformat.cpp
    src/clockformat.h
    src/clockwindow.cpp
    src/clockwindow.h
    src/main.cpp
    src/stopwatchpage.cpp
    src/stopwatchpage.h
    src/timers.cpp
    src/timers.h
    src/timerspage.cpp
    src/timerspage.h
)
target_link_libraries(karyo-clock PRIVATE Karyo::Style Qt6::DBus)
karyo_add_translation(karyo-clock "${CMAKE_CURRENT_SOURCE_DIR}/translations/karyo-clock_ru.ts")
install(TARGETS karyo-clock RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT clock-runtime)
install(FILES data/karyo-clock.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT clock-runtime)

if(BUILD_TESTING)
    qt_add_executable(karyo-clock-timertest tests/timertest.cpp src/timers.cpp src/timers.h)
    target_include_directories(karyo-clock-timertest PRIVATE src)
    target_link_libraries(karyo-clock-timertest PRIVATE Qt6::Core)
    add_test(NAME clock-timers COMMAND karyo-clock-timertest)
endif()

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-clock COMPONENT clock-runtime)
configure_file(data/clock.in "${CMAKE_CURRENT_BINARY_DIR}/clock" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/clock" DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT clock-runtime)
