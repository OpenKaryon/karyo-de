find_program(KARYO_LRELEASE NAMES lrelease lrelease6 HINTS /usr/lib/qt6/bin REQUIRED)
function(karyo_add_translation target source)
    get_filename_component(name "${source}" NAME_WE)
    set(qm "${CMAKE_CURRENT_BINARY_DIR}/${name}.qm")
    add_custom_command(OUTPUT "${qm}"
        COMMAND "${KARYO_LRELEASE}" "${source}" -qm "${qm}"
        DEPENDS "${source}" VERBATIM)
    qt_add_resources(${target} "${target}-translations"
        PREFIX /i18n BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES "${qm}")
endfunction()
