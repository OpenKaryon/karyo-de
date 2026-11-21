if(TARGET karyo-wifi)
    return()
endif()
find_package(Qt6 REQUIRED COMPONENTS DBus)
# Internal NetworkManager activation shared by shell and Settings.
add_library(karyo-wifi STATIC
    ${PROJECT_SOURCE_DIR}/src/network/wifi.cpp ${PROJECT_SOURCE_DIR}/src/network/wifi.h)
target_include_directories(karyo-wifi PUBLIC ${PROJECT_SOURCE_DIR}/src/network)
target_link_libraries(karyo-wifi PUBLIC Qt6::Core Qt6::DBus)
