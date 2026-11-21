add_library(karyo-appcenter-catalog STATIC src/catalog.cpp src/catalog.h)
target_include_directories(karyo-appcenter-catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(karyo-appcenter-catalog PUBLIC Qt6::Core)
if(BUILD_TESTING)
    add_executable(karyo-appcenter-catalog-test tests/catalogtest.cpp)
    target_link_libraries(karyo-appcenter-catalog-test PRIVATE karyo-appcenter-catalog Qt6::Test)
    add_test(NAME appcenter-external-catalog COMMAND karyo-appcenter-catalog-test)
endif()
# Curated public data is an optional external component, never a compiled resource.
install(FILES data/catalog.json DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo COMPONENT appcenter-catalog-runtime)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/docs/PROVENANCE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-appcenter-catalog COMPONENT appcenter-catalog-runtime)

add_library(karyo-appcenter-packages STATIC src/packages.cpp src/packages.h)
target_link_libraries(karyo-appcenter-packages PUBLIC karyo-appcenter-catalog karyo-app-command karyo-app-software Karyo::DesktopIntegration)

if(BUILD_TESTING)
    add_executable(karyo-appcenter-packages-test tests/packagestest.cpp)
    target_link_libraries(karyo-appcenter-packages-test PRIVATE karyo-appcenter-packages Qt6::Test)
    add_test(NAME appcenter-package-operations COMMAND karyo-appcenter-packages-test)
    set_tests_properties(appcenter-package-operations PROPERTIES TIMEOUT 45)
endif()
