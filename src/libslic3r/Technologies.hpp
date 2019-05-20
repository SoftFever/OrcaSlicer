#ifndef _technologies_h_
#define _technologies_h_

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
#define ENABLE_CAMERA_STATISTICS 1


//====================
// 1.42.0.alpha1 techs
//====================
#define ENABLE_1_42_0_ALPHA1 1

// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH (0 && ENABLE_1_42_0_ALPHA1)
// Disable imgui dialog for move, rotate and scale gizmos
#define DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI (1 && ENABLE_1_42_0_ALPHA1)
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING (0 && ENABLE_1_42_0_ALPHA1)


//====================
// 1.42.0.alpha4 techs
//====================
#define ENABLE_1_42_0_ALPHA4 1

// Changed algorithm to extract euler angles from rotation matrix
#define ENABLE_NEW_EULER_ANGLES (1 && ENABLE_1_42_0_ALPHA4)
// Modified initial default placement of generic subparts
#define ENABLE_GENERIC_SUBPARTS_PLACEMENT (1 && ENABLE_1_42_0_ALPHA4)
// Bunch of fixes related to volumes centering
#define ENABLE_VOLUMES_CENTERING_FIXES (1 && ENABLE_1_42_0_ALPHA4)


//====================
// 1.42.0.alpha7 techs
//====================
#define ENABLE_1_42_0_ALPHA7 1

// Printbed textures generated from svg files
#define ENABLE_TEXTURES_FROM_SVG (1 && ENABLE_1_42_0_ALPHA7)


//====================
// 1.42.0.alpha8 techs
//====================
#define ENABLE_1_42_0_ALPHA8 1

// Toolbars and Gizmos use icons imported from svg files
#define ENABLE_SVG_ICONS (1 && ENABLE_1_42_0_ALPHA8 && ENABLE_TEXTURES_FROM_SVG)


//====================
// 1.42.0.rc techs
//====================
#define ENABLE_1_42_0_RC 1

// Disables Edit->Deselect all item menu item
#define DISABLE_DESELECT_ALL_MENU_ITEM (1 && ENABLE_1_42_0_RC)

#endif // _technologies_h_
