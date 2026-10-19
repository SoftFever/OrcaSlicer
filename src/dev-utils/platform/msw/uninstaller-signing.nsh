; Lets the Windows release workflow ship a signed uninstaller (see signpath/README.md).
; makensis generates the uninstaller while it compiles the installer, so it cannot be
; signed afterwards like the installed binaries. Instead a first compile exports it
; (/DORCA_EXPORT_UNINSTALLER=<path>), the export is signed together with the binaries, and
; the final compile embeds the signed copy in place of the uninstaller it generates
; (/DORCA_SIGNED_UNINSTALLER=<path>). Both compiles run the same script over the same
; installed files, so they generate the same uninstaller. A failed copy aborts the
; compile rather than leave the uninstaller unsigned.
;
; cpack passes the paths with forward slashes, which cmd's copy does not accept for its
; source, so they are converted first.
!ifdef ORCA_EXPORT_UNINSTALLER
  !searchreplace ORCA_EXPORT_UNINSTALLER_PATH "${ORCA_EXPORT_UNINSTALLER}" "/" "\"
  !uninstfinalize 'cmd /c copy /y "%1" "${ORCA_EXPORT_UNINSTALLER_PATH}"' = 0
!endif
!ifdef ORCA_SIGNED_UNINSTALLER
  !searchreplace ORCA_SIGNED_UNINSTALLER_PATH "${ORCA_SIGNED_UNINSTALLER}" "/" "\"
  !uninstfinalize 'cmd /c copy /y "${ORCA_SIGNED_UNINSTALLER_PATH}" "%1"' = 0
!endif
