cmake_minimum_required(VERSION 3.8)
project(libslic3r_gui)

include(PrecompiledHeader)

set(SLIC3R_GUI_SOURCES
    pchheader.cpp
    pchheader.hpp
    GUI/AboutDialog.cpp
    GUI/AboutDialog.hpp
    GUI/SysInfoDialog.cpp
    GUI/SysInfoDialog.hpp
    GUI/KBShortcutsDialog.cpp
    GUI/KBShortcutsDialog.hpp
    GUI/AppConfig.cpp
    GUI/AppConfig.hpp
    GUI/BackgroundSlicingProcess.cpp
    GUI/BackgroundSlicingProcess.hpp
    GUI/BitmapCache.cpp
    GUI/BitmapCache.hpp
    GUI/ConfigSnapshotDialog.cpp
    GUI/ConfigSnapshotDialog.hpp
    GUI/3DScene.cpp
    GUI/3DScene.hpp
    GUI/format.hpp
    GUI/GLShader.cpp
    GUI/GLShader.hpp    
    GUI/GLCanvas3D.hpp
    GUI/GLCanvas3D.cpp
    GUI/OpenGLManager.hpp
    GUI/OpenGLManager.cpp
    GUI/Selection.hpp
    GUI/Selection.cpp    
    GUI/Gizmos/GLGizmosManager.cpp
    GUI/Gizmos/GLGizmosManager.hpp
    GUI/Gizmos/GLGizmosCommon.cpp
    GUI/Gizmos/GLGizmosCommon.hpp
    GUI/Gizmos/GLGizmoBase.cpp
    GUI/Gizmos/GLGizmoBase.hpp
    GUI/Gizmos/GLGizmoMove.cpp
    GUI/Gizmos/GLGizmoMove.hpp
    GUI/Gizmos/GLGizmoRotate.cpp
    GUI/Gizmos/GLGizmoRotate.hpp
    GUI/Gizmos/GLGizmoScale.cpp
    GUI/Gizmos/GLGizmoScale.hpp
    GUI/Gizmos/GLGizmoSlaSupports.cpp
    GUI/Gizmos/GLGizmoSlaSupports.hpp
    GUI/Gizmos/GLGizmoFdmSupports.cpp
    GUI/Gizmos/GLGizmoFdmSupports.hpp
    GUI/Gizmos/GLGizmoFlatten.cpp
    GUI/Gizmos/GLGizmoFlatten.hpp
    GUI/Gizmos/GLGizmoCut.cpp
    GUI/Gizmos/GLGizmoCut.hpp
    GUI/Gizmos/GLGizmoHollow.cpp
    GUI/Gizmos/GLGizmoHollow.hpp
    GUI/GLSelectionRectangle.cpp
    GUI/GLSelectionRectangle.hpp
    GUI/GLTexture.hpp
    GUI/GLTexture.cpp
    GUI/GLToolbar.hpp
    GUI/GLToolbar.cpp
    GUI/Preferences.cpp
    GUI/Preferences.hpp
    GUI/Preset.cpp
    GUI/Preset.hpp
    GUI/PresetBundle.cpp
    GUI/PresetBundle.hpp
    GUI/PresetHints.cpp
    GUI/PresetHints.hpp
    GUI/GUI.cpp
    GUI/GUI.hpp
    GUI/GUI_Preview.cpp
    GUI/GUI_Preview.hpp
    GUI/GUI_App.cpp
    GUI/GUI_App.hpp
    GUI/GUI_Utils.cpp
    GUI/GUI_Utils.hpp
    GUI/I18N.cpp
    GUI/I18N.hpp
    GUI/MainFrame.cpp
    GUI/MainFrame.hpp
    GUI/Plater.cpp
    GUI/Plater.hpp
    GUI/GUI_ObjectList.cpp
    GUI/GUI_ObjectList.hpp
    GUI/GUI_ObjectManipulation.cpp
    GUI/GUI_ObjectManipulation.hpp
    GUI/GUI_ObjectSettings.cpp
    GUI/GUI_ObjectSettings.hpp
    GUI/GUI_ObjectLayers.cpp
    GUI/GUI_ObjectLayers.hpp
    GUI/MeshUtils.cpp
    GUI/MeshUtils.hpp
    GUI/Tab.cpp
    GUI/Tab.hpp
    GUI/ConfigManipulation.cpp
    GUI/ConfigManipulation.hpp
    GUI/Field.cpp
    GUI/Field.hpp
    GUI/OptionsGroup.cpp
    GUI/OptionsGroup.hpp
    GUI/BedShapeDialog.cpp
    GUI/BedShapeDialog.hpp
    GUI/2DBed.cpp
    GUI/2DBed.hpp
    GUI/3DBed.cpp
    GUI/3DBed.hpp
    GUI/Camera.cpp
    GUI/Camera.hpp
    GUI/wxExtensions.cpp
    GUI/wxExtensions.hpp
    GUI/ExtruderSequenceDialog.cpp
    GUI/ExtruderSequenceDialog.hpp
    GUI/WipeTowerDialog.cpp
    GUI/WipeTowerDialog.hpp
    GUI/RammingChart.cpp
    GUI/RammingChart.hpp
    GUI/RemovableDriveManager.cpp
    GUI/RemovableDriveManager.hpp
    GUI/BonjourDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/ButtonsDescription.cpp
    GUI/ButtonsDescription.hpp
    GUI/ImGuiWrapper.hpp
    GUI/ImGuiWrapper.cpp
    Config/Snapshot.cpp
    Config/Snapshot.hpp
    Config/Version.cpp
    Config/Version.hpp    
    Utils/ASCIIFolding.cpp
    Utils/ASCIIFolding.hpp
    Utils/Serial.cpp
    Utils/Serial.hpp
    GUI/ConfigWizard.cpp
    GUI/ConfigWizard.hpp
    GUI/ConfigWizard_private.hpp
    GUI/MsgDialog.cpp
    GUI/MsgDialog.hpp
    GUI/UpdateDialogs.cpp
    GUI/UpdateDialogs.hpp
    GUI/FirmwareDialog.cpp
    GUI/FirmwareDialog.hpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/Jobs/Job.hpp
    GUI/Jobs/Job.cpp
    GUI/Jobs/ArrangeJob.hpp
    GUI/Jobs/ArrangeJob.cpp
    GUI/Jobs/RotoptimizeJob.hpp
    GUI/Jobs/RotoptimizeJob.cpp
    GUI/Jobs/SLAImportJob.hpp
    GUI/Jobs/SLAImportJob.cpp
    GUI/Jobs/ProgressIndicator.hpp
    GUI/ProgressStatusBar.hpp
    GUI/ProgressStatusBar.cpp
    GUI/Mouse3DController.cpp
    GUI/Mouse3DController.hpp
    GUI/DoubleSlider.cpp
    GUI/DoubleSlider.hpp
    GUI/ObjectDataViewModel.cpp
    GUI/ObjectDataViewModel.hpp
    GUI/InstanceCheck.cpp
    GUI/InstanceCheck.hpp
    GUI/Search.cpp
    GUI/Search.hpp
    GUI/NotificationManager.cpp
    GUI/NotificationManager.hpp
    Utils/Http.cpp
    Utils/Http.hpp
    Utils/FixModelByWin10.cpp
    Utils/FixModelByWin10.hpp
    Utils/OctoPrint.cpp
    Utils/OctoPrint.hpp
    Utils/Duet.cpp
    Utils/Duet.hpp
    Utils/FlashAir.cpp
    Utils/FlashAir.hpp
    Utils/AstroBox.cpp
    Utils/AstroBox.hpp
    Utils/PrintHost.cpp
    Utils/PrintHost.hpp
    Utils/Bonjour.cpp
    Utils/Bonjour.hpp
    Utils/PresetUpdater.cpp
    Utils/PresetUpdater.hpp
    Utils/UndoRedo.cpp
    Utils/UndoRedo.hpp
    Utils/HexFile.cpp
    Utils/HexFile.hpp
    Utils/Thread.hpp
    Utils/SLAImport.hpp
    Utils/SLAImport.cpp
)

if (APPLE)
    list(APPEND SLIC3R_GUI_SOURCES
            Utils/RetinaHelperImpl.mm
            Utils/MacDarkMode.mm
            GUI/RemovableDriveManagerMM.mm
            GUI/RemovableDriveManagerMM.h
            GUI/Mouse3DHandlerMac.mm
            GUI/InstanceCheckMac.mm
            GUI/InstanceCheckMac.h
        )
    FIND_LIBRARY(DISKARBITRATION_LIBRARY DiskArbitration)

endif ()

add_library(libslic3r_gui STATIC ${SLIC3R_GUI_SOURCES})

encoding_check(libslic3r_gui)

target_link_libraries(libslic3r_gui libslic3r avrdude cereal imgui GLEW::GLEW OpenGL::GL OpenGL::GLU hidapi libcurl ${wxWidgets_LIBRARIES})

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(libslic3r_gui ${DBUS_LIBRARIES}) 
endif()

if (SLIC3R_STATIC)
    # FIXME: This was previously exported by wx-config but the wxWidgets
    # cmake build forgets this and the build fails in debug mode (or on raspberry release)
    target_compile_definitions(libslic3r_gui PUBLIC -DwxDEBUG_LEVEL=0)
endif()

if(APPLE)
    target_link_libraries(libslic3r_gui ${DISKARBITRATION_LIBRARY})
endif()

if (SLIC3R_STATIC AND UNIX AND NOT APPLE)
    target_compile_definitions(libslic3r_gui PRIVATE OPENSSL_CERT_OVERRIDE)
endif ()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_gui pchheader.hpp FORCEINCLUDE)
endif ()
