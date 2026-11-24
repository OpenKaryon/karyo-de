cmake_minimum_required(VERSION 3.21)
project(KaryoLoginNetwork VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)

find_path(PAM_INCLUDE_DIR security/pam_modules.h REQUIRED)
find_library(PAM_LIBRARY pam REQUIRED)
add_library(pam_karyo_network MODULE pam_karyo_network.cpp)
set_target_properties(pam_karyo_network PROPERTIES PREFIX "" CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO AUTOMOC OFF)
target_compile_definitions(pam_karyo_network PRIVATE _GNU_SOURCE)
target_compile_options(pam_karyo_network PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(pam_karyo_network PRIVATE "${PAM_INCLUDE_DIR}")
target_link_libraries(pam_karyo_network PRIVATE "${PAM_LIBRARY}")
install(TARGETS pam_karyo_network LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/security
    COMPONENT profile-login-runtime)
install(FILES ../../LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-login
    COMPONENT profile-login-runtime)

if(BUILD_TESTING)
    add_executable(karyo-native-pam-harness ../tests/native_pam_harness.cpp)
    set_target_properties(karyo-native-pam-harness PROPERTIES CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO AUTOMOC OFF)
    target_compile_options(karyo-native-pam-harness PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(karyo-native-pam-harness PRIVATE "${PAM_INCLUDE_DIR}")
    target_link_libraries(karyo-native-pam-harness PRIVATE "${PAM_LIBRARY}")
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME profile-login-module-abi COMMAND ${Python3_EXECUTABLE} -B
        "${CMAKE_CURRENT_SOURCE_DIR}/module_abi.py" "$<TARGET_FILE:pam_karyo_network>")
    # Root/native acceptance is explicit, never an automatic CTest side effect.
endif()
