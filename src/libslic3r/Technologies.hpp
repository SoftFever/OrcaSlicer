#ifndef _technologies_h_
#define _technologies_h_

//============
// debug techs
//============

// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0

//====================
// 1.42.0.alpha1 techs
//====================
#define ENABLE_1_42_0_ALPHA1 1

// Uses a unique opengl context
#define ENABLE_USE_UNIQUE_GLCONTEXT (1 && ENABLE_1_42_0_ALPHA1)
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH (0 && ENABLE_1_42_0_ALPHA1)
// Keeps objects on bed while scaling them using the scale gizmo
#define ENABLE_ENSURE_ON_BED_WHILE_SCALING (1 && ENABLE_1_42_0_ALPHA1)
// All rotations made using the rotate gizmo are done with respect to the world reference system
#define ENABLE_WORLD_ROTATIONS (1 && ENABLE_1_42_0_ALPHA1)
// Scene's GUI made using imgui library
#define ENABLE_IMGUI (1 && ENABLE_1_42_0_ALPHA1)
#define DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI (1 && ENABLE_IMGUI)
// Modified Sla support gizmo
#define ENABLE_SLA_SUPPORT_GIZMO_MOD (1 && ENABLE_1_42_0_ALPHA1)
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING (0 && ENABLE_1_42_0_ALPHA1)
// Renders a small sphere in the center of the bounding box of the current selection when no gizmo is active
#define ENABLE_RENDER_SELECTION_CENTER (0 && ENABLE_1_42_0_ALPHA1)
// Show visual hints in the 3D scene when sidebar matrix fields have focus
#define ENABLE_SIDEBAR_VISUAL_HINTS (1 && ENABLE_1_42_0_ALPHA1)
// Separate rendering for opaque and transparent volumes
#define ENABLE_IMPROVED_TRANSPARENT_VOLUMES_RENDERING (1 && ENABLE_1_42_0_ALPHA1)

//====================
// 1.42.0.alpha2 techs
//====================
#define ENABLE_1_42_0_ALPHA2 1

// Improves navigation between sidebar fields
#define ENABLE_IMPROVED_SIDEBAR_OBJECTS_MANIPULATION (1 && ENABLE_1_42_0_ALPHA2)
// Adds print bed models to 3D scene
#define ENABLE_PRINT_BED_MODELS (0 && ENABLE_1_42_0_ALPHA2)
#endif // _technologies_h_


//====================
// 1.42.0.alpha4 techs
//====================
#define ENABLE_1_42_0_ALPHA4 1

// Changed algorithm to extract euler angles from rotation matrix
#define ENABLE_NEW_EULER_ANGLES (1 && ENABLE_1_42_0_ALPHA4)
// Added minimum threshold for click and drag movements
#define ENABLE_MOVE_MIN_THRESHOLD (1 && ENABLE_1_42_0_ALPHA4)
