# Internal shared file labels/tiles for the shell, Files and Archive.
add_library(karyo-file-presentation STATIC
    ${PROJECT_SOURCE_DIR}/src/files/filetypes.cpp ${PROJECT_SOURCE_DIR}/src/files/filetypes.h)
target_include_directories(karyo-file-presentation PUBLIC ${PROJECT_SOURCE_DIR}/src/files)
target_link_libraries(karyo-file-presentation PUBLIC Karyo::UiFoundation)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(karyo-file-type-test ${PROJECT_SOURCE_DIR}/tests/filetypetest.cpp)
    target_link_libraries(karyo-file-type-test PRIVATE karyo-file-presentation Qt6::Test)
    add_test(NAME shared-file-types COMMAND karyo-file-type-test)
    set_tests_properties(shared-file-types PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
