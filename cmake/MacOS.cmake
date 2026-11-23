get_filename_component(macos_source "${CMAKE_CURRENT_LIST_DIR}/../integrations/macos" ABSOLUTE)
configure_file("${macos_source}/entrypoint.in" "${CMAKE_CURRENT_BINARY_DIR}/karyo-macos" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-macos" DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT macos-runtime)
install(DIRECTORY "${macos_source}/karyo_macos" DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/macos
    COMPONENT macos-runtime FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
install(FILES "${macos_source}/../../LICENSE" "${macos_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-macos-core COMPONENT macos-runtime)
if(BUILD_TESTING)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME macos-launch-policy COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${macos_source}/tests -v)
    add_test(NAME macos-process-streams COMMAND ${Python3_EXECUTABLE} -B ${macos_source}/tests/test_process_streams.py -v)
    add_test(NAME macos-swift-source-inputs COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${macos_source}/runtime/tests -v)
    set_tests_properties(macos-swift-source-inputs PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    set_tests_properties(macos-launch-policy PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

if(TARGET Karyo::Style)
    add_subdirectory("${macos_source}/picker" "${CMAKE_CURRENT_BINARY_DIR}/macos-picker")
endif()
