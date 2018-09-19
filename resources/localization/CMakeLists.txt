set(L10N_DIR "${PROJECT_SOURCE_DIR}/resources/localization")
add_custom_target(pot
    COMMAND xgettext --keyword=L --from-code=UTF-8 --debug
        -f "${L10N_DIR}/list.txt"
        -o "${L10N_DIR}/Slic3rPE.pot"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generate pot file from strings in the source tree"
)
