# Internal shared clipboard and background file operations; no exported SDK dependency.
add_library(karyo-file-operations STATIC
    ${PROJECT_SOURCE_DIR}/src/files/fileops.cpp ${PROJECT_SOURCE_DIR}/src/files/fileops.h)
target_include_directories(karyo-file-operations PUBLIC ${PROJECT_SOURCE_DIR}/src/files)
target_link_libraries(karyo-file-operations PUBLIC Qt6::Widgets)
if(BUILD_TESTING)
    add_executable(karyo-file-operations-test ${PROJECT_SOURCE_DIR}/tests/fileoperationstest.cpp)
    target_link_libraries(karyo-file-operations-test PRIVATE karyo-file-operations Qt6::Test)
    add_test(NAME shared-file-operations COMMAND karyo-file-operations-test)
    set_tests_properties(shared-file-operations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
endif()
