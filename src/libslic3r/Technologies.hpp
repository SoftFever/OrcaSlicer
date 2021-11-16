#ifndef _prusaslicer_technologies_h_
#define _prusaslicer_technologies_h_

//=============
// debug techs
//=============
// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0
// Renders a small sphere in the center of the bounding box of the current selection when no gizmo is active
#define ENABLE_RENDER_SELECTION_CENTER 0
// Shows an imgui dialog with camera related data
#define ENABLE_CAMERA_STATISTICS 0
// Render the picking pass instead of the main scene (use [T] key to toggle between regular rendering and picking pass only rendering)
#define ENABLE_RENDER_PICKING_PASS 0
// Enable extracting thumbnails from selected gcode and save them as png files
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG 0
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH 0
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING 0
// Enable G-Code viewer statistics imgui dialog
#define ENABLE_GCODE_VIEWER_STATISTICS 0
// Enable G-Code viewer comparison between toolpaths height and width detected from gcode and calculated at gcode generation 
#define ENABLE_GCODE_VIEWER_DATA_CHECKING 0
// Enable project dirty state manager debug window
#define ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW 0


// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP 0
// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS 0
// Enable rendering markers for options in preview as fixed screen size points
#define ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS 1


//====================
// 2.4.0.alpha1 techs
//====================
#define ENABLE_2_4_0_ALPHA1 1

// Enable implementation of retract acceleration in gcode processor
#define ENABLE_RETRACT_ACCELERATION (1 && ENABLE_2_4_0_ALPHA1)
// Enable rendering seams (and other options) in preview using models
#define ENABLE_SEAMS_USING_MODELS (1 && ENABLE_2_4_0_ALPHA1)
// Enable save and save as commands to be enabled also when the plater is empty and allow to load empty projects
#define ENABLE_SAVE_COMMANDS_ALWAYS_ENABLED (1 && ENABLE_2_4_0_ALPHA1)


//====================
// 2.4.0.alpha2 techs
//====================
#define ENABLE_2_4_0_ALPHA2 1

// Enable rendering seams (and other options) in preview using batched models on systems not supporting OpenGL 3.3
#define ENABLE_SEAMS_USING_BATCHED_MODELS (1 && ENABLE_SEAMS_USING_MODELS && ENABLE_2_4_0_ALPHA2)
// Enable fixing the z position of color change, pause print and custom gcode markers in preview
#define ENABLE_FIX_PREVIEW_OPTIONS_Z (1 && ENABLE_SEAMS_USING_MODELS && ENABLE_2_4_0_ALPHA2)
// Enable replacing a missing file during reload from disk command
#define ENABLE_RELOAD_FROM_DISK_REPLACE_FILE (1 && ENABLE_2_4_0_ALPHA2)
// Enable fixing the synchronization of seams with the horizontal slider in preview
#define ENABLE_FIX_SEAMS_SYNCH (1 && ENABLE_2_4_0_ALPHA2)


//====================
// 2.4.0.alpha3 techs
//====================
#define ENABLE_2_4_0_ALPHA3 1

// Enable fixing loading of gcode files generated with SuperSlicer in GCodeViewer
#define ENABLE_FIX_SUPERSLICER_GCODE_IMPORT (1 && ENABLE_2_4_0_ALPHA3)


//====================
// 2.4.0.beta1 techs
//====================
#define ENABLE_2_4_0_BETA1 1

// Enable rendering modifiers and similar objects always as transparent
#define ENABLE_MODIFIERS_ALWAYS_TRANSPARENT (1 && ENABLE_2_4_0_BETA1)


//====================
// 2.4.0.beta2 techs
//====================
#define ENABLE_2_4_0_BETA2 1

// Enable modified ImGuiWrapper::slider_float() to create a compound widget where
// an additional button can be used to set the keyboard focus into the slider
// to allow the user to type in the desired value
#define ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT (1 && ENABLE_2_4_0_BETA2)

#endif // _prusaslicer_technologies_h_
