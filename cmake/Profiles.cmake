# Low-level optional accounts component. Installing it does not enable PAM,
# create users, start sessions or replace a display manager.
get_filename_component(profile_source "${CMAKE_CURRENT_LIST_DIR}/../profiles" ABSOLUTE)
foreach(operation IN ITEMS add list switch sync password-changed)
    set(KARYO_PROFILE_OPERATION "${operation}")
    configure_file("${profile_source}/entrypoint.in" "${CMAKE_CURRENT_BINARY_DIR}/desktop-${operation}" @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/desktop-${operation}"
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo COMPONENT profile-accounts-runtime)
endforeach()
configure_file("${profile_source}/frontend.in" "${CMAKE_CURRENT_BINARY_DIR}/karyo-desktops" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-desktops"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-accounts-runtime)
install(DIRECTORY "${profile_source}/karyo_profiles"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/profiles
    COMPONENT profile-accounts-runtime FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
configure_file("${profile_source}/org.karyo.desktops.policy.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.karyo.desktops.policy" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.karyo.desktops.policy"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions COMPONENT profile-accounts-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-accounts COMPONENT profile-accounts-runtime)
configure_file("${profile_source}/karyo-desktops-sync.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-desktops-sync" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/karyo-desktops-sync"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pam-configs COMPONENT profile-password-sync-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-password-sync
    COMPONENT profile-password-sync-runtime)

# Separate opt-in PAM/entry payload. The library package enables no service.
configure_file("${profile_source}/session-entry.in"
    "${CMAKE_CURRENT_BINARY_DIR}/profile-session-entry" @ONLY)
configure_file("${profile_source}/session-wait.in"
    "${CMAKE_CURRENT_BINARY_DIR}/profile-session-wait" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/profile-session-entry"
    "${CMAKE_CURRENT_BINARY_DIR}/profile-session-wait"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo COMPONENT profile-session-runtime)
install(FILES "${profile_source}/pam.d/karyo-desktop"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/pam.d COMPONENT profile-session-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-session
    COMPONENT profile-session-runtime)

# Inactive opt-in host transport. System units use lib, not the architecture
# library directory (lib/x86_64-linux-gnu is not a systemd unit search path).
set(KARYO_SYSTEMD_SYSTEM_UNIT_DIR "lib/systemd/system" CACHE STRING "systemd system unit installation directory")
# The socket is root-only; installation does not enable it.
configure_file("${profile_source}/host.in"
    "${CMAKE_CURRENT_BINARY_DIR}/profile-host" @ONLY)
configure_file("${profile_source}/karyo-profile-host.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-host.service" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/profile-host"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo COMPONENT profile-host-runtime)
install(FILES "${profile_source}/karyo-profile-host.socket"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-host.service"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR} COMPONENT profile-host-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-host COMPONENT profile-host-runtime)

if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME profile-account-policy COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${profile_source}/tests -v)
    set_tests_properties(profile-account-policy PROPERTIES TIMEOUT 45)
endif()
