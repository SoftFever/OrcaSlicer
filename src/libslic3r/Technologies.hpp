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


// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP 0
// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS 0
// Enable rendering markers for options in preview as fixed screen size points
#define ENABLE_FIXED_SCREEN_SIZE_POINT_MARKERS 1


//====================
// 2.4.0.alpha0 techs
//====================
#define ENABLE_2_4_0_ALPHA0 1

// Enable reload from disk command for 3mf files
#define ENABLE_RELOAD_FROM_DISK_FOR_3MF (1 && ENABLE_2_4_0_ALPHA0)
// Enable showing gcode line numbers in preview horizontal slider
#define ENABLE_GCODE_LINES_ID_IN_H_SLIDER (1 && ENABLE_2_4_0_ALPHA0)
// Enable validation of custom gcode against gcode processor reserved keywords
#define ENABLE_VALIDATE_CUSTOM_GCODE (1 && ENABLE_2_4_0_ALPHA0)
// Enable showing a imgui window containing gcode in preview
#define ENABLE_GCODE_WINDOW (1 && ENABLE_2_4_0_ALPHA0)
// Enable exporting lines M73 for remaining time to next printer stop to gcode
#define ENABLE_EXTENDED_M73_LINES (1 && ENABLE_VALIDATE_CUSTOM_GCODE)
// Enable a modified version of automatic downscale on load of objects too big
#define ENABLE_MODIFIED_DOWNSCALE_ON_LOAD_OBJECTS_TOO_BIG (1 && ENABLE_2_4_0_ALPHA0)
// Enable scrollable legend in preview
#define ENABLE_SCROLLABLE_LEGEND (1 && ENABLE_2_4_0_ALPHA0)
// Enable visualization of start gcode as regular toolpaths
#define ENABLE_START_GCODE_VISUALIZATION (1 && ENABLE_2_4_0_ALPHA0)
// Enable visualization of seams in preview
#define ENABLE_SEAMS_VISUALIZATION (1 && ENABLE_2_4_0_ALPHA0)
// Enable project dirty state manager
#define ENABLE_PROJECT_DIRTY_STATE (1 && ENABLE_2_4_0_ALPHA0)
// Enable project dirty state manager debug window
#define ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW (0 && ENABLE_PROJECT_DIRTY_STATE)
// Enable to push object instances under the bed
#define ENABLE_ALLOW_NEGATIVE_Z (1 && ENABLE_2_4_0_ALPHA0)
#define DISABLE_ALLOW_NEGATIVE_Z_FOR_SLA (1 && ENABLE_ALLOW_NEGATIVE_Z)
// Enable visualization of objects clearance for sequential prints
#define ENABLE_SEQUENTIAL_LIMITS (1 && ENABLE_2_4_0_ALPHA0)


#endif // _prusaslicer_technologies_h_
