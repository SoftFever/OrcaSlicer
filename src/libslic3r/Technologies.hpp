#ifndef _technologies_h_
#define _technologies_h_

//============
// debug techs
//============

// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0

//=============
// 1.42.0 techs
//=============
#define ENABLE_1_42_0 1

// Uses a unique opengl context
#define ENABLE_USE_UNIQUE_GLCONTEXT (1 && ENABLE_1_42_0)
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH (0 && ENABLE_1_42_0)
// Modified camera target behavior
#define ENABLE_MODIFIED_CAMERA_TARGET (1 && ENABLE_1_42_0)
// Add Geometry::Transformation class and use it into ModelInstance, ModelVolume and GLVolume
#define ENABLE_MODELVOLUME_TRANSFORM (1 && ENABLE_1_42_0)
// Keeps objects on bed while scaling them using the scale gizmo
#define ENABLE_ENSURE_ON_BED_WHILE_SCALING (1 && ENABLE_MODELVOLUME_TRANSFORM)
// All rotations made using the rotate gizmo are done with respect to the world reference system
#define ENABLE_WORLD_ROTATIONS (1 && ENABLE_1_42_0)
// Scene's GUI made using imgui library
#define ENABLE_IMGUI (1 && ENABLE_1_42_0)
#define DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI (1 && ENABLE_IMGUI)
// Modified Sla support gizmo
#define ENABLE_SLA_SUPPORT_GIZMO_MOD (1 && ENABLE_1_42_0)
// Removes the wxNotebook from plater
#define ENABLE_REMOVE_TABS_FROM_PLATER (1 && ENABLE_1_42_0)
// Constrains the camera target into the scene bounding box
#define ENABLE_CONSTRAINED_CAMERA_TARGET (1 && ENABLE_1_42_0)

#endif // _technologies_h_


