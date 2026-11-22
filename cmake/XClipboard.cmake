# Optional ordinary-user X11/Wayland transport, independently of profiles/VMware.
get_filename_component(xclipboard_source "${CMAKE_CURRENT_LIST_DIR}/../integrations/clipboard" ABSOLUTE)
configure_file("${xclipboard_source}/entrypoint.in" "${CMAKE_CURRENT_BINARY_DIR}/karyo-xclipboard" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-xclipboard"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT xclipboard-runtime)
install(DIRECTORY "${xclipboard_source}/karyo_xclipboard"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/clipboard
    COMPONENT xclipboard-runtime FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES "${xclipboard_source}/../../LICENSE" "${xclipboard_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-xclipboard COMPONENT xclipboard-runtime)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME xclipboard-protocol COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${xclipboard_source}/tests -v)
    set_tests_properties(xclipboard-protocol PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()
