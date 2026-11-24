cmake_minimum_required(VERSION 3.21)
project(KaryoVMblockShutdown VERSION 0.1.0 LANGUAGES NONE)
include(GNUInstallDirs)
include(CTest)
configure_file(karyo-vmblock-shutdown.service.in karyo-vmblock-shutdown.service @ONLY)
install(PROGRAMS shutdown_guard.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo
    RENAME vmblock-shutdown-guard COMPONENT vmware-shutdown-runtime)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/karyo-vmblock-shutdown.service
    DESTINATION lib/systemd/system COMPONENT vmware-shutdown-runtime)
install(FILES 50-karyo-vmblock.conf DESTINATION lib/systemd/system/session-.scope.d
    COMPONENT vmware-shutdown-runtime)
install(FILES ../../../LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-vmware-shutdown
    COMPONENT vmware-shutdown-runtime)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME vmware-shutdown COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v)
endif()
