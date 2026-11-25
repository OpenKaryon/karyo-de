# Low-level optional accounts component. Installing it does not enable PAM,
# create users, start sessions or replace a display manager.
get_filename_component(profile_source "${CMAKE_CURRENT_LIST_DIR}/../profiles" ABSOLUTE)
# Keep the independent account-only project free of a compiler dependency.
# The full DE opts in to building this separate, inactive install component.
option(KARYO_BUILD_PROFILE_LOGIN "Build the inactive main-login PAM handoff component" OFF)
if(KARYO_BUILD_PROFILE_LOGIN)
    enable_language(CXX)
    add_subdirectory("${profile_source}/pam" "${CMAKE_CURRENT_BINARY_DIR}/profile-login")
endif()
foreach(operation IN ITEMS add list switch network-info sync password-changed)
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
    COMPONENT profile-accounts-runtime FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE
    PATTERN "clipboard.py" EXCLUDE PATTERN "clipboard_client.py" EXCLUDE)
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
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d COMPONENT profile-session-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-session
    COMPONENT profile-session-runtime)

# Inactive opt-in host transport. System units use lib, not the architecture
# library directory (lib/x86_64-linux-gnu is not a systemd unit search path).
set(KARYO_SYSTEMD_SYSTEM_UNIT_DIR "lib/systemd/system" CACHE STRING "systemd system unit installation directory")
configure_file("${profile_source}/lightdm_entry.in" "${CMAKE_CURRENT_BINARY_DIR}/lightdm-entry" @ONLY)
configure_file("${profile_source}/lightdm_service.conf.in" "${CMAKE_CURRENT_BINARY_DIR}/lightdm-service.conf" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/lightdm-entry"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo COMPONENT profile-lightdm-runtime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lightdm-service.conf"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR}/lightdm.service.d
    RENAME 60-karyo-desktops.conf COMPONENT profile-lightdm-runtime)
install(FILES "${profile_source}/lightdm_console.conf"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/lightdm/lightdm.conf.d
    RENAME 90-karyo-desktops.conf COMPONENT profile-lightdm-runtime)
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

# A distinct, inactive payload: only an explicit administrator command enables
# private desktop forwarding and the host socket. Base DE installation is inert.
configure_file("${profile_source}/runtime.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-runtime" @ONLY)
configure_file("${profile_source}/karyo-profile-networking.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-networking.service" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-runtime"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-runtime)
configure_file("${profile_source}/main_login.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-main-login" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-main-login"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-runtime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-networking.service"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR} COMPONENT profile-runtime)
install(FILES "${profile_source}/karyo-profile-host-runtime.conf"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR}/karyo-profile-host.service.d
    RENAME 50-profile-runtime.conf COMPONENT profile-runtime)
install(FILES "${profile_source}/org.karyo.ProfileNetwork.conf"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d COMPONENT profile-runtime)
configure_file("${profile_source}/physical_network.in"
    "${CMAKE_CURRENT_BINARY_DIR}/physical-network" @ONLY)
configure_file("${profile_source}/org.karyo.PhysicalNetwork.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.karyo.PhysicalNetwork.service" @ONLY)
configure_file("${profile_source}/karyo-physical-network.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-physical-network.service" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/physical-network"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo COMPONENT profile-runtime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.karyo.PhysicalNetwork.service"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services COMPONENT profile-runtime)
install(FILES "${profile_source}/org.karyo.PhysicalNetwork.conf"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d COMPONENT profile-runtime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/karyo-physical-network.service"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR} COMPONENT profile-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-runtime COMPONENT profile-runtime)

# Optional ordinary-user provider clients; installation starts no daemon.
foreach(KARYO_NETWORK_CLIENT IN ITEMS nmcli connections context)
    configure_file("${profile_source}/network_client.in"
        "${CMAKE_CURRENT_BINARY_DIR}/karyo-network-${KARYO_NETWORK_CLIENT}" @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-network-${KARYO_NETWORK_CLIENT}"
        DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-accounts-runtime)
endforeach()
set(KARYO_NETWORK_CLIENT nmcli)
configure_file("${profile_source}/network_client.in" "${CMAKE_CURRENT_BINARY_DIR}/network-clients/nmcli" @ONLY)
set(KARYO_NETWORK_CLIENT connections)
configure_file("${profile_source}/network_client.in" "${CMAKE_CURRENT_BINARY_DIR}/network-clients/nm-connection-editor" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/network-clients/nmcli"
    "${CMAKE_CURRENT_BINARY_DIR}/network-clients/nm-connection-editor"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/network-clients COMPONENT profile-accounts-runtime)
install(FILES "${profile_source}/network_connections.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/profile-catalog/network/applications
    RENAME nm-connection-editor.desktop COMPONENT profile-accounts-runtime)
foreach(KARYO_NETBIRD_CLIENT IN ITEMS cli gui context)
    configure_file("${profile_source}/netbird-client.in"
        "${CMAKE_CURRENT_BINARY_DIR}/karyo-netbird-${KARYO_NETBIRD_CLIENT}" @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-netbird-${KARYO_NETBIRD_CLIENT}"
        DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-netbird-client)
endforeach()
foreach(KARYO_NETBIRD_CLIENT IN ITEMS cli gui)
    if(KARYO_NETBIRD_CLIENT STREQUAL "cli")
        set(netbird_alias netbird)
    else()
        set(netbird_alias netbird-ui)
    endif()
    configure_file("${profile_source}/netbird-client.in"
        "${CMAKE_CURRENT_BINARY_DIR}/network-clients/${netbird_alias}" @ONLY)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/network-clients/${netbird_alias}"
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/network-clients COMPONENT profile-accounts-runtime)
endforeach()
install(FILES "${profile_source}/netbird_host_guard.conf"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR}/netbird.service.d
    RENAME 50-karyo-private-desktops.conf COMPONENT profile-netbird-client)
configure_file("${profile_source}/netbird.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/org.wails.netbird.desktop" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.wails.netbird.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/profile-catalog/netbird/applications
    COMPONENT profile-netbird-client)
configure_file("${profile_source}/netbird-opener.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-netbird-xdg-open" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-netbird-xdg-open"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/netbird-opener
    RENAME xdg-open COMPONENT profile-netbird-client)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-netbird-client COMPONENT profile-netbird-client)

if(BUILD_TESTING)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_test(NAME profile-networkd-cooperation COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_networkd_cooperation.py -v)
    set_tests_properties(profile-networkd-cooperation PROPERTIES TIMEOUT 20)
    add_test(NAME profile-account-policy COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${profile_source}/tests -v)
    set_tests_properties(profile-account-policy PROPERTIES TIMEOUT 45)
    add_test(NAME profile-live-dns COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_dns_refresh.py -v)
    add_test(NAME profile-network-budget COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_network_budget.py -v)
    add_test(NAME profile-carrier-refresh COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_route_refresh.py -v)
    add_test(NAME profile-provider-recovery COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_provider_recovery.py -v)
    add_test(NAME profile-physical-network COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_physical_network.py -v)
    add_test(NAME profile-network-info COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_network_info.py -v)
    add_test(NAME profile-main-entry COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_main_entry.py -v)
    add_test(NAME profile-main-activation COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_main_activation.py -v)
    add_test(NAME profile-network-service-recovery COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_network_service_recovery.py -v)
    add_test(NAME profile-main-network-startup COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_main_network_startup.py -v)
    add_test(NAME profile-display-manager-partition COMMAND ${Python3_EXECUTABLE} -B -m unittest discover
        -s ${profile_source}/tests -p test_display_manager.py -v)
    set_tests_properties(profile-live-dns profile-network-budget profile-carrier-refresh
        profile-network-info profile-main-entry profile-main-activation profile-network-service-recovery
        profile-main-network-startup profile-display-manager-partition PROPERTIES TIMEOUT 20)
endif()

# Separate administrator opt-in. No service reload/restart at installation.
configure_file("${profile_source}/networkd_entry.in" "${CMAKE_CURRENT_BINARY_DIR}/karyo-networkd-entry" @ONLY)
configure_file("${profile_source}/networkd_exec.conf.in" "${CMAKE_CURRENT_BINARY_DIR}/networkd_exec.conf" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-networkd-entry"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo RENAME networkd-entry COMPONENT profile-networkd-runtime)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/networkd_exec.conf"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR}/systemd-networkd.service.d
    RENAME 30-karyo-desktops.conf COMPONENT profile-networkd-runtime)
install(FILES "${profile_source}/networkd_cooperation.conf"
    DESTINATION lib/systemd/networkd.conf.d RENAME 30-karyo-desktops.conf COMPONENT profile-networkd-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-networkd COMPONENT profile-networkd-runtime)


# Ordinary, optional role metadata. No policy is installed or activated here.
configure_file("${profile_source}/application-catalog.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-application-catalog" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-application-catalog"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-accounts-runtime)

# Separate opt-in family clipboard. No service starts at package installation.
configure_file("${profile_source}/clipboard.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-clipboard" @ONLY)
configure_file("${profile_source}/karyo-profile-clipboard.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-clipboard.service" @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-clipboard"
    DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT profile-clipboard-runtime)
install(FILES "${profile_source}/karyo_profiles/clipboard.py"
    "${profile_source}/karyo_profiles/clipboard_client.py"
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/karyo/profiles/karyo_profiles COMPONENT profile-clipboard-runtime)
install(FILES "${profile_source}/karyo-profile-clipboard.socket"
    "${CMAKE_CURRENT_BINARY_DIR}/karyo-profile-clipboard.service"
    DESTINATION ${KARYO_SYSTEMD_SYSTEM_UNIT_DIR} COMPONENT profile-clipboard-runtime)
install(FILES "${profile_source}/../LICENSE" "${profile_source}/README.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/karyo-profile-clipboard COMPONENT profile-clipboard-runtime)
