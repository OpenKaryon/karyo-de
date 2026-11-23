# SPDX-License-Identifier: GPL-3.0-or-later
include_guard(GLOBAL)

# Keep launchers available to other desktops without changing their implicit
# MIME defaults. Karyo adds the full entries through its own XDG data directory.
function(karyo_install_desktop_entry source component)
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(name "${source}" NAME)
    file(READ "${source}" contents)
    if(contents MATCHES "\nMimeType=")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop-entries")
        string(REGEX REPLACE "\nMimeType=[^\n]*" "" launcher "${contents}")
        set(global "${CMAKE_CURRENT_BINARY_DIR}/desktop-entries/${name}")
        file(WRITE "${global}" "${launcher}")
        install(FILES "${global}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT ${component})
        install(FILES "${source}" DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/associations/applications COMPONENT ${component})
    else()
        install(FILES "${source}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT ${component})
    endif()
endfunction()

# Added Associations belongs in an unprefixed mimeapps.list. Desktop-specific
# lists may contain defaults only. Generate from the same public app metadata,
# including optional handlers; absent optional launchers are ignored by GIO.
function(karyo_install_session_associations root)
    file(GLOB entries CONFIGURE_DEPENDS "${root}/apps/*/data/*.desktop"
        "${root}/integrations/win32/*.desktop" "${root}/integrations/macos/picker/data/*.desktop")
    list(SORT entries)
    set(types)
    foreach(entry IN LISTS entries)
        get_filename_component(name "${entry}" NAME)
        file(STRINGS "${entry}" lines REGEX "^MimeType=")
        foreach(line IN LISTS lines)
            string(REGEX REPLACE "^MimeType=" "" entry_types "${line}")
            foreach(type IN LISTS entry_types)
                if(NOT type STREQUAL "")
                    string(SHA256 key "${type}")
                    list(APPEND types "${type}")
                    list(APPEND handlers_${key} "${name}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES types)
    list(SORT types)
    set(associations "")
    foreach(type IN LISTS types)
        string(SHA256 key "${type}")
        list(REMOVE_DUPLICATES handlers_${key})
        list(JOIN handlers_${key} ";" handlers)
        string(APPEND associations "${type}=${handlers};\n")
    endforeach()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop-associations")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/desktop-associations/mimeapps.list")
    # xdg-mime's generic fallback otherwise needs a private mimeinfo.cache.
    # Explicit defaults in this session-only directory work without that cache;
    # the user's higher-priority configuration still takes precedence.
    file(WRITE "${output}" "[Default Applications]\n${associations}\n[Added Associations]\n${associations}")
    install(FILES "${output}" DESTINATION ${CMAKE_INSTALL_DATADIR}/karyo/associations/applications COMPONENT session-runtime)
endfunction()
