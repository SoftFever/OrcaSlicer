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
// Shows an imgui dialog with render related data
#define ENABLE_RENDER_STATISTICS 0
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


//=================
// 2.2.0.rc1 techs
//=================
#define ENABLE_2_2_0_RC1 1

// Enable hack to remove crash when closing on OSX 10.9.5
#define ENABLE_HACK_CLOSING_ON_OSX_10_9_5 (1 && ENABLE_2_2_0_RC1)


//====================
// 2.3.0.alpha1 techs
//====================
#define ENABLE_2_3_0_ALPHA1 1

// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP (0 && ENABLE_2_3_0_ALPHA1)

// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS (0 && ENABLE_2_3_0_ALPHA1)

// Enable error logging for OpenGL calls when SLIC3R_LOGLEVEL >= 5
#define ENABLE_OPENGL_ERROR_LOGGING (1 && ENABLE_2_3_0_ALPHA1)

// Enable built-in DPI changed event handler of wxWidgets 3.1.3
#define ENABLE_WX_3_1_3_DPI_CHANGED_EVENT (1 && ENABLE_2_3_0_ALPHA1)


//====================
// 2.3.0.alpha3 techs
//====================
#define ENABLE_2_3_0_ALPHA3 1

#define ENABLE_CTRL_M_ON_WINDOWS (1 && ENABLE_2_3_0_ALPHA3)


//====================
// 2.3.0.alpha4 techs
//====================
#define ENABLE_2_3_0_ALPHA4 1

#define ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS (1 && ENABLE_2_3_0_ALPHA4)
#define ENABLE_SHOW_OPTION_POINT_LAYERS (1 && ENABLE_2_3_0_ALPHA4)


//===================
// 2.3.0.beta1 techs
//===================
#define ENABLE_2_3_0_BETA1 1

#define ENABLE_SHOW_WIPE_MOVES (1 && ENABLE_2_3_0_BETA1)
#define ENABLE_DRAG_AND_DROP_FIX (1 && ENABLE_2_3_0_BETA1)
#define ENABLE_CUSTOMIZABLE_FILES_ASSOCIATION_ON_WIN (1 && ENABLE_2_3_0_BETA1)


//===================
// 2.3.0.beta2 techs
//===================
#define ENABLE_2_3_0_BETA2 1

#define ENABLE_ARROW_KEYS_WITH_SLIDERS (1 && ENABLE_2_3_0_BETA2) 


#endif // _prusaslicer_technologies_h_
