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

//====================
// 1.42.0.alpha1 techs
//====================
#define ENABLE_1_42_0_ALPHA1 1

// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH (0 && ENABLE_1_42_0_ALPHA1)
// Scene's GUI made using imgui library
#define ENABLE_IMGUI (1 && ENABLE_1_42_0_ALPHA1)
#define DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI (1 && ENABLE_IMGUI)
// Modified Sla support gizmo
#define ENABLE_SLA_SUPPORT_GIZMO_MOD (1 && ENABLE_1_42_0_ALPHA1)
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING (0 && ENABLE_1_42_0_ALPHA1)

//====================
// 1.42.0.alpha2 techs
//====================
#define ENABLE_1_42_0_ALPHA2 1

// Improves navigation between sidebar fields
#define ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION (1 && ENABLE_1_42_0_ALPHA2)
// Adds print bed models to 3D scene
#define ENABLE_PRINT_BED_MODELS (1 && ENABLE_1_42_0_ALPHA2)
#endif // _technologies_h_


//====================
// 1.42.0.alpha4 techs
//====================
#define ENABLE_1_42_0_ALPHA4 1

// Changed algorithm to extract euler angles from rotation matrix
#define ENABLE_NEW_EULER_ANGLES (1 && ENABLE_1_42_0_ALPHA4)
// Added minimum threshold for click and drag movements
#define ENABLE_MOVE_MIN_THRESHOLD (1 && ENABLE_1_42_0_ALPHA4)
// Modified initial default placement of generic subparts
#define ENABLE_GENERIC_SUBPARTS_PLACEMENT (1 && ENABLE_1_42_0_ALPHA4)
// Reworked management of bed shape changes
#define ENABLE_REWORKED_BED_SHAPE_CHANGE (1 && ENABLE_1_42_0_ALPHA4)
// Use anisotropic filtering on bed plate texture
#define ENABLE_ANISOTROPIC_FILTER_ON_BED_TEXTURES (1 && ENABLE_1_42_0_ALPHA4)
// Bunch of fixes related to volumes centering
#define ENABLE_VOLUMES_CENTERING_FIXES (1 && ENABLE_1_42_0_ALPHA4)
