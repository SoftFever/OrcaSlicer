#ifndef _technologies_h_
#define _technologies_h_

// 1.42.0 techs
#define ENABLE_1_42_0 1

// Add z coordinate to model instances' offset
// Add x and y rotation components to model instances' offset
// Add scaling factors for all the three axes to model instances
#define ENABLE_MODELINSTANCE_3D_FULL_TRANSFORM (1 && ENABLE_1_42_0)
// Add double click on gizmo grabbers to reset transformation components to their default value
#define ENABLE_GIZMOS_RESET (1 && ENABLE_1_42_0)

#endif // _technologies_h_


