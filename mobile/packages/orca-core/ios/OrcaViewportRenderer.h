#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, OrcaViewportMode) {
  OrcaViewportModeScene = 0,
  OrcaViewportModePreview = 1,
};

typedef NS_ENUM(NSInteger, OrcaPreviewColorMode) {
  OrcaPreviewColorModeFeature   = 0,
  OrcaPreviewColorModeSpeed     = 1,
  OrcaPreviewColorModeFlow      = 2,
  OrcaPreviewColorModeLayerTime = 3,
  OrcaPreviewColorModeTool      = 4,
};

/// Draws a session's bed, objects and toolpath into an MTKView. Reads geometry from the
/// native session by id (see SessionRegistry), never through JavaScript. Plain
/// Objective-C interface so the Swift Nitro View can use it; the C++ lives in the .mm.
@interface OrcaViewportRenderer : NSObject <MTKViewDelegate>

- (instancetype)initWithView:(MTKView *)view;

@property(nonatomic) uint64_t sessionId;
@property(nonatomic) OrcaViewportMode mode;
/// Lowest layer id drawn in preview mode; negative starts at 0.
@property(nonatomic) NSInteger minLayer;
/// Highest layer id drawn in preview mode; negative draws every layer.
@property(nonatomic) NSInteger maxLayer;
/// Highest print-time (seconds from start) drawn in preview mode; negative draws every move.
@property(nonatomic) float maxTime;
@property(nonatomic) BOOL showTravels;
@property(nonatomic) BOOL showRetractions;
@property(nonatomic) BOOL showWipes;
@property(nonatomic) OrcaPreviewColorMode colorMode;
/// Highlight this object with a warm tint; 0 clears the selection.
@property(nonatomic) uint64_t selectedId;

/// Re-reads bed, objects and toolpath from the session and redraws.
- (void)reload;
/// Frames the bed and everything on it.
- (void)fit;
/// Camera gestures, in points.
- (void)orbitBy:(CGPoint)delta;
- (void)panBy:(CGPoint)delta;
- (void)zoomBy:(CGFloat)factor;

/// Converts a point in the view's local coordinate space (points, top-left
/// origin, matches touch events) to a world-space ray.
- (void)screenToRayFromX:(CGFloat)x
                       y:(CGFloat)y
               originOut:(vector_float3*)originOut
            directionOut:(vector_float3*)directionOut;

/// Picks the topmost object under the given screen point. Returns 0 on miss.
/// Runs on the calling thread; must not be called while the session is under
/// a long-running write (slice / import / arrange).
- (uint64_t)pickObjectAtScreenX:(CGFloat)x y:(CGFloat)y;

/// Intersects the world-space ray at (x, y) with the plate plane (Z = 0) and
/// writes the intersection's XY to the out params. Returns NO when the ray is
/// parallel to the plate — in that case the outs are untouched.
- (BOOL)screenToPlateXYFromX:(CGFloat)x y:(CGFloat)y outX:(CGFloat*)outX outY:(CGFloat*)outY;

/// Begins a drag on the given object. Captures the object's current transform
/// and the anchor point on the plate under the finger. Returns NO if the object
/// no longer exists or the touch does not intersect the plate.
- (BOOL)beginDragObject:(uint64_t)objectId atScreenX:(CGFloat)x y:(CGFloat)y;

/// Moves the currently-dragging object so that the initial anchor point on the
/// object follows the finger. No-op when no drag is in flight.
- (void)dragToScreenX:(CGFloat)x y:(CGFloat)y;

/// Ends the drag, taking a single undo snapshot for the whole move. No-op when
/// no drag is in flight.
- (void)endDrag;

/// YES while a drag is being tracked; the Swift layer reads this to decide
/// whether a 1-finger pan should route to orbit or to drag.
@property(nonatomic, readonly) BOOL isDragging;

/// Chooses what a 1-finger drag on the selected object does. 0 = move,
/// 1 = rotate (around Z), 2 = uniform scale. Ignored while a drag is in flight
/// so the current gesture keeps its tool until release.
- (void)setObjectTool:(NSInteger)tool;

@end

NS_ASSUME_NONNULL_END
