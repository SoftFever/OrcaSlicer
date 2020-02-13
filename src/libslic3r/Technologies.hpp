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
#define ENABLE_CAMERA_STATISTICS 0
//  Render the picking pass instead of the main scene (use [T] key to toggle between regular rendering and picking pass only rendering)
#define ENABLE_RENDER_PICKING_PASS 0


//====================
// 1.42.0.alpha1 techs
//====================
#define ENABLE_1_42_0_ALPHA1 1

// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH (0 && ENABLE_1_42_0_ALPHA1)
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING (0 && ENABLE_1_42_0_ALPHA1)


//====================
// 2.2.0.alpha1 techs
//====================
#define ENABLE_2_2_0_ALPHA1 1

// Enable thumbnail generator
// When removing this technology, remove it also from stable branch, 
// where it has been partially copied for patch 2.1.1
#define ENABLE_THUMBNAIL_GENERATOR (1 && ENABLE_2_2_0_ALPHA1)
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG (0 && ENABLE_THUMBNAIL_GENERATOR)


//==================
// 2.2.0.beta1 techs
//==================
#define ENABLE_2_2_0_BETA1 1

// Enable showing object/instance info with labels into the 3D scene
#define ENABLE_SHOW_SCENE_LABELS (1 && ENABLE_2_2_0_BETA1)

#endif // _technologies_h_
