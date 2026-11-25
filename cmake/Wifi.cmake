if(TARGET karyo-wifi)
    return()
endif()
find_package(Qt6 REQUIRED COMPONENTS DBus)
# Internal NetworkManager activation shared by shell and Settings.
add_library(karyo-wifi STATIC
    ${PROJECT_SOURCE_DIR}/src/network/wifi.cpp ${PROJECT_SOURCE_DIR}/src/network/wifi.h
    ${PROJECT_SOURCE_DIR}/src/network/physical_client.cpp ${PROJECT_SOURCE_DIR}/src/network/physical_client.h
    ${PROJECT_SOURCE_DIR}/src/network/physical_panel.cpp ${PROJECT_SOURCE_DIR}/src/network/physical_panel.h)
target_include_directories(karyo-wifi PUBLIC ${PROJECT_SOURCE_DIR}/src/network)
target_link_libraries(karyo-wifi PUBLIC Qt6::Core Qt6::DBus Qt6::Widgets)
if(BUILD_TESTING)
    add_executable(karyo-physical-network-test ${PROJECT_SOURCE_DIR}/tests/physical_network_test.cpp)
    target_link_libraries(karyo-physical-network-test PRIVATE karyo-wifi Qt6::Test)
    add_test(NAME physical-network-controls COMMAND dbus-run-session -- /bin/sh
        ${PROJECT_SOURCE_DIR}/tests/run-private-bus.sh $<TARGET_FILE:karyo-physical-network-test>)
    set_tests_properties(physical-network-controls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()
