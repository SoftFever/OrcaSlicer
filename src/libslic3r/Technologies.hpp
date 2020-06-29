#ifndef _prusaslicer_technologies_h_
#define _prusaslicer_technologies_h_

//============
// debug techs
//============

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


//================
// 2.2.0.rc1 techs
//================
#define ENABLE_2_2_0_RC1 1

// Enable hack to remove crash when closing on OSX 10.9.5
#define ENABLE_HACK_CLOSING_ON_OSX_10_9_5 (1 && ENABLE_2_2_0_RC1)


//===================
// 2.3.0.alpha1 techs
//===================
#define ENABLE_2_3_0_ALPHA1 1

// Enable rendering of objects colored by facets' slope
#define ENABLE_SLOPE_RENDERING (1 && ENABLE_2_3_0_ALPHA1)

// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP (1 && ENABLE_2_3_0_ALPHA1)

// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS (0 && ENABLE_2_3_0_ALPHA1)

// Enable error logging for OpenGL calls when SLIC3R_LOGLEVEL >= 5
#define ENABLE_OPENGL_ERROR_LOGGING (1 && ENABLE_2_3_0_ALPHA1)

// Enable built-in DPI changed event handler of wxWidgets 3.1.3
#define ENABLE_WX_3_1_3_DPI_CHANGED_EVENT (1 && ENABLE_2_3_0_ALPHA1)

// Enable changing application layout without the need to restart
#define ENABLE_LAYOUT_NO_RESTART (1 && ENABLE_2_3_0_ALPHA1)

// Enable G-Code viewer
#define ENABLE_GCODE_VIEWER (1 && ENABLE_2_3_0_ALPHA1)
#define ENABLE_GCODE_VIEWER_STATISTICS (1 && ENABLE_GCODE_VIEWER)
#define ENABLE_GCODE_VIEWER_SHADERS_EDITOR (1 && ENABLE_GCODE_VIEWER)
#define ENABLE_GCODE_VIEWER_AS_STATE (1 && ENABLE_GCODE_VIEWER)


#endif // _prusaslicer_technologies_h_
