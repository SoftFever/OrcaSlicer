#ifndef _technologies_h_
#define _technologies_h_

//============
// debug techs
//============

// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0

//=============
// 1.42.0 techs
//=============
#define ENABLE_1_42_0 1

// Add double click on gizmo grabbers to reset transformation components to their default value
#define ENABLE_GIZMOS_RESET (1 && ENABLE_1_42_0)
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
// Gizmos always rendered on top of objects
#define ENABLE_GIZMOS_ON_TOP (1 && ENABLE_1_42_0)
// New menu layout (open/save/save as project + import/export)
#define ENABLE_NEW_MENU_LAYOUT (1 && ENABLE_1_42_0)
// All rotations made using the rotate gizmo are done with respect to the world reference system
#define ENABLE_WORLD_ROTATIONS (1 && ENABLE_1_42_0)

#endif // _technologies_h_


