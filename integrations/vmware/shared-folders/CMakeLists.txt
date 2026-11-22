cmake_minimum_required(VERSION 3.21)
project(KaryoVMwareSharedFolders VERSION 0.1.0 LANGUAGES NONE)
include(GNUInstallDirs)
include(CTest)
configure_file(karyo-vmhgfs.service.in karyo-vmhgfs.service @ONLY)
install(PROGRAMS mount_shared_folders.py DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo
    RENAME vmware-shared-folders COMPONENT vmware-shared-folders-runtime)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/karyo-vmhgfs.service DESTINATION lib/systemd/system
    COMPONENT vmware-shared-folders-runtime)
install(FILES ../../../LICENSE README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-vmware-shared-folders
    COMPONENT vmware-shared-folders-runtime)
if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME vmware-shared-folder-ownership COMMAND ${Python3_EXECUTABLE} -B -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v)
endif()
