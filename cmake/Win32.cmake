get_filename_component(win32_source "${CMAKE_CURRENT_LIST_DIR}/../integrations/win32" ABSOLUTE)
configure_file("${win32_source}/entrypoint.in" "${CMAKE_CURRENT_BINARY_DIR}/karyo-win32" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-win32" DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT win32-runtime)
install(DIRECTORY "${win32_source}/karyo_win32" DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/win32
    COMPONENT win32-runtime FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES "${win32_source}/karyo-win32.desktop" "${win32_source}/karyo-mimeapps.list"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT win32-runtime)
install(FILES "${win32_source}/karyo-win32.xml" DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages COMPONENT win32-runtime)
install(FILES "${win32_source}/../../LICENSE" "${win32_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-win32 COMPONENT win32-runtime)
# Independent caption resources used by the optional native compositor patches.
install(DIRECTORY "${win32_source}/theme/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/themes/Karyo-Windows/labwc
    COMPONENT win32-runtime FILES_MATCHING PATTERN "*.svg")
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME win32-launch-policy COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${win32_source}/tests -v)
    set_tests_properties(win32-launch-policy PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()
