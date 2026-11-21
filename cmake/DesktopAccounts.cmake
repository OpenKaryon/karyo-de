if(TARGET karyo-desktop-accounts)
    return()
endif()
# Internal read-only model shared by Settings and optional session/greeter integration.
add_library(karyo-desktop-accounts STATIC
    ${PROJECT_SOURCE_DIR}/src/accounts/desktopaccounts.cpp ${PROJECT_SOURCE_DIR}/src/accounts/desktopaccounts.h
    ${PROJECT_SOURCE_DIR}/src/accounts/profileconfig.cpp ${PROJECT_SOURCE_DIR}/src/accounts/profileconfig.h
    ${PROJECT_SOURCE_DIR}/src/accounts/desktopsettings.cpp ${PROJECT_SOURCE_DIR}/src/accounts/desktopsettings.h)
target_include_directories(karyo-desktop-accounts PUBLIC ${PROJECT_SOURCE_DIR}/src/accounts)
target_link_libraries(karyo-desktop-accounts PUBLIC Qt6::Core)
if(BUILD_TESTING)
    add_executable(karyo-desktop-accounts-test ${PROJECT_SOURCE_DIR}/tests/desktopaccountstest.cpp)
    target_link_libraries(karyo-desktop-accounts-test PRIVATE karyo-desktop-accounts Qt6::Test)
    target_compile_definitions(karyo-desktop-accounts-test PRIVATE
        KARYO_PROFILE_CORPUS="${PROJECT_SOURCE_DIR}/profiles/tests/policy-corpus.json")
    add_test(NAME desktop-account-discovery COMMAND karyo-desktop-accounts-test)
    set_tests_properties(desktop-account-discovery PROPERTIES TIMEOUT 15)
endif()
