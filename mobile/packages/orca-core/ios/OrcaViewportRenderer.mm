#import "OrcaViewportRenderer.h"

#include "SessionRegistry.hpp"

#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>
#import <simd/simd.h>

#include <algorithm>
#include <cmath>
#include <vector>

// The shader is compiled at runtime so the pod needs no metallib packaging.
static const char* const kShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  packed_float3 position;
  packed_float3 normal;
  packed_float4 color;
};

struct TexturedVertexIn {
  packed_float3 position;
  packed_float2 uv;
};

struct Uniforms {
  float4x4 mvp;
  float4 lightDir;
  float4 params; // x: 1 for lit triangles, 0 for lines
  float4 modelOffset; // xyz added to every vertex; used to slide the dragged object each frame without a mesh rebuild
  float4 dragAnchor;  // xyz: world-space pivot the rotate/scale is performed around
  float4 dragParams;  // x: rotation-Z radians added at draw time, y: uniform scale multiplier, zw: padding
};

struct VertexOut {
  float4 position [[position]];
  float4 color;
};

// One toolpath move, drawn as a line between its endpoints. One record per
// segment, so the layer slider only changes which range of the buffer is drawn.
struct Segment {
  packed_float3 a;
  packed_float3 b;
  uchar4 color;
};

struct TexturedOut {
  float4 position [[position]];
  float2 uv;
};

vertex VertexOut orca_vertex(const device VertexIn* vertices [[buffer(0)]],
                             constant Uniforms& u [[buffer(1)]],
                             uint vid [[vertex_id]]) {
  VertexIn v = vertices[vid];
  VertexOut out;
  // Compose the per-draw model transform: rotate around the anchor by
  // dragParams.x radians (Z axis), then uniformly scale by dragParams.y from
  // the same anchor, then translate by modelOffset. For the non-dragged
  // triangles the caller passes anchor=(0,0,0), dragParams=(0,1) so this
  // collapses to identity + offset (offset is also zero for them).
  float3 rel = float3(v.position) - u.dragAnchor.xyz;
  float c = cos(u.dragParams.x);
  float s = sin(u.dragParams.x);
  float3 rot;
  rot.x = c * rel.x - s * rel.y;
  rot.y = s * rel.x + c * rel.y;
  rot.z = rel.z;
  float3 world = rot * u.dragParams.y + u.dragAnchor.xyz + u.modelOffset.xyz;
  out.position = u.mvp * float4(world, 1.0);
  float shade = 1.0;
  if (u.params.x > 0.5) {
    float3 n = normalize(float3(v.normal));
    shade = 0.35 + 0.65 * max(dot(n, normalize(u.lightDir.xyz)), 0.0);
  }
  float4 col = float4(v.color);
  out.color = float4(col.rgb * shade, col.a);
  return out;
}

vertex VertexOut orca_segment_line_vertex(const device Segment* segments [[buffer(0)]],
                                          constant Uniforms& u [[buffer(1)]],
                                          uint vid [[vertex_id]],
                                          uint iid [[instance_id]]) {
  Segment s = segments[iid];
  VertexOut out;
  out.position = u.mvp * float4(vid == 0 ? float3(s.a) : float3(s.b), 1.0);
  out.color = float4(s.color) / 255.0;
  return out;
}

fragment float4 orca_fragment(VertexOut in [[stage_in]]) {
  return in.color;
}

vertex TexturedOut orca_bed_vertex(const device TexturedVertexIn* vertices [[buffer(0)]],
                                    constant Uniforms& u [[buffer(1)]],
                                    uint vid [[vertex_id]]) {
  TexturedVertexIn v = vertices[vid];
  TexturedOut out;
  out.position = u.mvp * float4(float3(v.position) + u.modelOffset.xyz, 1.0);
  out.uv = float2(v.uv);
  return out;
}

fragment float4 orca_bed_fragment(TexturedOut in [[stage_in]],
                                   texture2d<float> tex [[texture(0)]],
                                   sampler samp [[sampler(0)]]) {
  return tex.sample(samp, in.uv);
}
)metal";

namespace {

struct Vertex {
  float position[3];
  float normal[3];
  float color[4];
};

struct TexturedVertex {
  float position[3];
  float uv[2];
};

// Mirrors the shader's Segment: 28 bytes, 4-byte aligned, so any segment's offset is a
// valid vertex buffer offset.
struct Segment {
  float a[3];
  float b[3];
  uint8_t color[4];
};
static_assert(sizeof(Segment) == 28, "Segment must match the shader's layout");

struct Uniforms {
  simd_float4x4 mvp;
  simd_float4 lightDir;
  simd_float4 params;
  simd_float4 modelOffset;
  simd_float4 dragAnchor;
  simd_float4 dragParams;
};

struct Bounds {
  simd_float3 min{1e30f, 1e30f, 1e30f};
  simd_float3 max{-1e30f, -1e30f, -1e30f};
  bool valid = false;
  void add(simd_float3 p) {
    min = simd_min(min, p);
    max = simd_max(max, p);
    valid = true;
  }
  simd_float3 center() const { return (min + max) * 0.5f; }
  float radius() const { return valid ? simd_length(max - min) * 0.5f : 100.f; }
};

simd_float4x4 perspective(float fovyRadians, float aspect, float nearZ, float farZ) {
  const float ys = 1.f / tanf(fovyRadians * 0.5f);
  const float xs = ys / aspect;
  const float zs = farZ / (nearZ - farZ);
  return simd_matrix(simd_make_float4(xs, 0, 0, 0), simd_make_float4(0, ys, 0, 0), simd_make_float4(0, 0, zs, -1),
                     simd_make_float4(0, 0, nearZ * zs, 0));
}

simd_float4x4 lookAt(simd_float3 eye, simd_float3 target, simd_float3 up) {
  const simd_float3 z = simd_normalize(eye - target);
  const simd_float3 x = simd_normalize(simd_cross(up, z));
  const simd_float3 y = simd_cross(z, x);
  return simd_matrix(simd_make_float4(x.x, y.x, z.x, 0), simd_make_float4(x.y, y.y, z.y, 0), simd_make_float4(x.z, y.z, z.z, 0),
                     simd_make_float4(-simd_dot(x, eye), -simd_dot(y, eye), -simd_dot(z, eye), 1));
}

// Colours per PreviewRole, in enum order; the last entry is used for travel moves.
const float kRoleColors[][4] = {
    {0.50f, 0.50f, 0.50f, 1.f}, // None
    {1.00f, 0.90f, 0.30f, 1.f}, // Perimeter
    {1.00f, 0.49f, 0.22f, 1.f}, // ExternalPerimeter
    {0.12f, 0.12f, 1.00f, 1.f}, // OverhangPerimeter
    {0.69f, 0.19f, 0.16f, 1.f}, // InternalInfill
    {0.59f, 0.33f, 0.80f, 1.f}, // SolidInfill
    {0.94f, 0.25f, 0.25f, 1.f}, // TopSolidInfill
    {0.40f, 0.36f, 0.78f, 1.f}, // BottomSurface
    {1.00f, 0.55f, 0.41f, 1.f}, // Ironing
    {0.30f, 0.50f, 0.73f, 1.f}, // BridgeInfill
    {0.30f, 0.50f, 0.73f, 1.f}, // InternalBridgeInfill
    {1.00f, 1.00f, 1.00f, 1.f}, // GapFill
    {0.00f, 0.53f, 0.43f, 1.f}, // Skirt
    {0.00f, 0.53f, 0.43f, 1.f}, // Brim
    {0.00f, 1.00f, 0.00f, 1.f}, // Support
    {0.00f, 0.50f, 0.00f, 1.f}, // SupportInterface
    {0.35f, 0.60f, 0.30f, 1.f}, // SupportTransition
    {0.70f, 0.65f, 0.15f, 1.f}, // WipeTower
    {0.37f, 0.82f, 0.58f, 1.f}, // Custom
    {0.50f, 0.50f, 0.50f, 1.f}, // Mixed
    {0.10f, 0.60f, 1.00f, 1.f}, // Travel
};
constexpr size_t kTravelColor = sizeof(kRoleColors) / sizeof(kRoleColors[0]) - 1;

void push(std::vector<Vertex>& out, simd_float3 p, simd_float3 n, const float* c) {
  out.push_back(Vertex{{p.x, p.y, p.z}, {n.x, n.y, n.z}, {c[0], c[1], c[2], c[3]}});
}

// Appends the segment a→b and records the move it belongs to, which is what the
// layer and time filters select on.
void pushSegment(std::vector<Segment>& out, std::vector<uint32_t>& moves, size_t move, const float* a, const float* b,
                 const float* c) {
  auto byte = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
  out.push_back(Segment{{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {byte(c[0]), byte(c[1]), byte(c[2]), byte(c[3])}});
  moves.push_back(uint32_t(move));
}

// Uploads `items`, or returns nil for an empty list (Metal rejects zero-length buffers).
template <typename T> id<MTLBuffer> makeBuffer(id<MTLDevice> device, const std::vector<T>& items) {
  return items.empty() ? nil
                       : [device newBufferWithBytes:items.data() length:items.size() * sizeof(T) options:MTLResourceStorageModeShared];
}

} // namespace

@interface OrcaViewportRenderer () <WKNavigationDelegate>
@end

@implementation OrcaViewportRenderer {
  __weak MTKView* _view;
  id<MTLDevice> _device;
  id<MTLCommandQueue> _queue;
  id<MTLRenderPipelineState> _pipeline;
  id<MTLRenderPipelineState> _segmentLinePipeline;
  id<MTLRenderPipelineState> _bedTexturePipeline;
  id<MTLDepthStencilState> _depthState;
  id<MTLSamplerState> _bedSampler;
  MTKTextureLoader* _textureLoader;

  id<MTLBuffer> _triangleBuffer;
  NSUInteger _triangleVertexCount;
  id<MTLBuffer> _bedLineBuffer;
  NSUInteger _bedLineVertexCount;
  // Toolpath segments in G-code order, extrusions and the other moves (travel, wipe,
  // retraction ticks) apart, each with the index of the move it came from.
  // `_*Range` is the slice the layer and time filters currently select.
  id<MTLBuffer> _extrusionBuffer;
  std::vector<uint32_t> _extrusionMoves;
  NSRange _extrusionRange;
  id<MTLBuffer> _markerBuffer;
  std::vector<uint32_t> _markerMoves;
  NSRange _markerRange;
  id<MTLBuffer> _bedModelBuffer;
  NSUInteger _bedModelVertexCount;
  id<MTLBuffer> _bedTextureQuadBuffer;
  id<MTLTexture> _bedTexture;
  NSString* _bedTextureLoadedPath;
  // Kept alive while the offscreen WKWebView renders an SVG bed art file.
  WKWebView* _svgRasterWebView;
  id<WKNavigationDelegate> _svgNavigationDelegate;

  Slic3r::Mobile::PreviewData _preview;
  Bounds _bounds;

  // Orbit camera, z up.
  simd_float3 _target;
  float _yaw;
  float _pitch;
  float _distance;

  // Drag-to-move/rotate/scale state. Non-zero id while a drag is in flight.
  // The interaction is chosen by `_dragTool`, and every drag frame just updates
  // the drag-* fields that the shader applies at draw time — no per-frame mesh
  // rebuild. The dragged object's triangle range in `_triangleBuffer` is
  // captured at reload so the draw pass can split into three ranges.
  uint64_t _dragObjectId;
  NSInteger _dragTool; // 0 = move, 1 = rotate, 2 = scale
  simd_float3 _dragInitialPosition;
  simd_float3 _dragInitialRotation;
  simd_float3 _dragInitialScale;
  simd_float2 _dragAnchorXY;   // world-space anchor point on the plate for move deltas
  CGPoint     _dragStartScreen; // finger's initial screen position (for rotate / scale mapping)
  simd_float3 _dragOffset;      // world-space translation applied by 'move'
  float       _dragRotationZ;   // additional Z rotation applied by 'rotate' (radians)
  float       _dragScaleFactor; // uniform scale multiplier applied by 'scale' (default 1)
  NSUInteger  _dragTriStart;
  NSUInteger  _dragTriCount;
}

- (instancetype)initWithView:(MTKView*)view {
  if ((self = [super init])) {
    _view = view;
    _device = MTLCreateSystemDefaultDevice();
    _queue = [_device newCommandQueue];
    _minLayer = -1;
    _maxLayer = -1;
    _maxTime = -1.f;
    _colorMode = OrcaPreviewColorModeFeature;
    _target = simd_make_float3(0, 0, 0);
    // Camera in the -Y front quadrant, slightly to the right (+X), so bed models
    // authored with their front-facing labels on the -Y edge read correctly. Match
    // desktop's convention: eye = (sin·cos, cos·cos, sin) · distance so
    // yaw ≈ π − 0.6 gives (sin > 0, cos < 0) — front-right-looking-into +Y.
    _target = simd_make_float3(0, 0, 0);
    _yaw = float(M_PI) - 0.6f;
    _pitch = 0.5f;
    _distance = 400.f;

    view.device = _device;
    view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    view.clearColor = MTLClearColorMake(0.93, 0.94, 0.95, 1.0);
    view.enableSetNeedsDisplay = YES;
    view.paused = YES;
    view.delegate = self;

    NSError* error = nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:[NSString stringWithUTF8String:kShaderSource] options:nil error:&error];
    if (library == nil) {
      NSLog(@"OrcaViewport: shader compile failed: %@", error);
      return self;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = [library newFunctionWithName:@"orca_vertex"];
    desc.fragmentFunction = [library newFunctionWithName:@"orca_fragment"];
    desc.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    desc.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    _pipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (_pipeline == nil) {
      NSLog(@"OrcaViewport: pipeline failed: %@", error);
    }
    desc.vertexFunction = [library newFunctionWithName:@"orca_segment_line_vertex"];
    _segmentLinePipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (_segmentLinePipeline == nil) {
      NSLog(@"OrcaViewport: segment pipeline failed: %@", error);
    }
    MTLRenderPipelineDescriptor* bedDesc = [MTLRenderPipelineDescriptor new];
    bedDesc.vertexFunction = [library newFunctionWithName:@"orca_bed_vertex"];
    bedDesc.fragmentFunction = [library newFunctionWithName:@"orca_bed_fragment"];
    bedDesc.colorAttachments[0].pixelFormat = view.colorPixelFormat;
    bedDesc.depthAttachmentPixelFormat = view.depthStencilPixelFormat;
    _bedTexturePipeline = [_device newRenderPipelineStateWithDescriptor:bedDesc error:&error];
    if (_bedTexturePipeline == nil) {
      NSLog(@"OrcaViewport: bed texture pipeline failed: %@", error);
    }
    MTLSamplerDescriptor* samp = [MTLSamplerDescriptor new];
    samp.minFilter = MTLSamplerMinMagFilterLinear;
    samp.magFilter = MTLSamplerMinMagFilterLinear;
    samp.mipFilter = MTLSamplerMipFilterLinear;
    samp.sAddressMode = MTLSamplerAddressModeClampToEdge;
    samp.tAddressMode = MTLSamplerAddressModeClampToEdge;
    _bedSampler = [_device newSamplerStateWithDescriptor:samp];
    _textureLoader = [[MTKTextureLoader alloc] initWithDevice:_device];
    MTLDepthStencilDescriptor* depth = [MTLDepthStencilDescriptor new];
    depth.depthCompareFunction = MTLCompareFunctionLessEqual;
    depth.depthWriteEnabled = YES;
    _depthState = [_device newDepthStencilStateWithDescriptor:depth];
  }
  return self;
}

#pragma mark - Properties

- (void)setMinLayer:(NSInteger)minLayer {
  _minLayer = minLayer;
  [self updateToolpathRange];
}

- (void)setMaxLayer:(NSInteger)maxLayer {
  _maxLayer = maxLayer;
  [self updateToolpathRange];
}

- (void)setMaxTime:(float)maxTime {
  _maxTime = maxTime;
  [self updateToolpathRange];
}

- (void)setShowTravels:(BOOL)showTravels {
  _showTravels = showTravels;
  [self rebuildToolpath];
}

- (void)setShowRetractions:(BOOL)showRetractions {
  _showRetractions = showRetractions;
  [self rebuildToolpath];
}

- (void)setShowWipes:(BOOL)showWipes {
  _showWipes = showWipes;
  [self rebuildToolpath];
}

- (void)setColorMode:(OrcaPreviewColorMode)colorMode {
  _colorMode = colorMode;
  [self rebuildToolpath];
}

- (void)setSelectedId:(uint64_t)selectedId {
  if (_selectedId == selectedId) return;
  _selectedId = selectedId;
  // Selection colour is baked into the triangle buffer, so we need a
  // full geometry rebuild to swap tints.
  [self reload];
}

#pragma mark - Geometry

- (void)reload {
  std::vector<Vertex> triangles;
  std::vector<Vertex> bed;
  std::vector<Vertex> bedModel;
  std::vector<TexturedVertex> bedQuad;
  std::string bedTexturePath;
  Slic3r::Mobile::PreviewData preview;
  Bounds bounds;
  // Range within `triangles` that belongs to the object currently being dragged.
  // Populated inside the SessionRegistry block below when a drag is in flight,
  // stays zero otherwise.
  size_t dragStart = 0;
  size_t dragCount = 0;
  bool hasCustomBed = false;

  const bool previewMode = _mode == OrcaViewportModePreview;
  const bool found = margelo::nitro::orca::SessionRegistry::with(_sessionId, [&](Slic3r::Mobile::Session& session) {
    // Bed outline and a 10 mm grid.
    const Slic3r::Mobile::BedInfo info = session.bed();
    const float grey[4] = {0.55f, 0.58f, 0.60f, 1.f};
    const float light[4] = {0.78f, 0.80f, 0.82f, 1.f};
    const simd_float3 up = simd_make_float3(0, 0, 1);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    if (info.shape.size() >= 2) {
      for (size_t i = 0; i < info.shape.size(); ++i) {
        const auto& a = info.shape[i];
        const auto& b = info.shape[(i + 1) % info.shape.size()];
        push(bed, simd_make_float3(float(a[0]), float(a[1]), 0), up, grey);
        push(bed, simd_make_float3(float(b[0]), float(b[1]), 0), up, grey);
        minX = std::min(minX, float(a[0]));
        maxX = std::max(maxX, float(a[0]));
        minY = std::min(minY, float(a[1]));
        maxY = std::max(maxY, float(a[1]));
      }
      for (float x = std::ceil(minX / 10.f) * 10.f; x < maxX; x += 10.f) {
        push(bed, simd_make_float3(x, minY, 0), up, light);
        push(bed, simd_make_float3(x, maxY, 0), up, light);
      }
      for (float y = std::ceil(minY / 10.f) * 10.f; y < maxY; y += 10.f) {
        push(bed, simd_make_float3(minX, y, 0), up, light);
        push(bed, simd_make_float3(maxX, y, 0), up, light);
      }
      bounds.add(simd_make_float3(minX, minY, 0));
      bounds.add(simd_make_float3(maxX, maxY, 0));
    }
    hasCustomBed = ! info.custom_texture.empty() || ! info.model_mesh.positions.empty();
    // Preloaded custom bed model (STL) — draw as a translucent grey slab so it
    // sits under the plate outline and reads as the printer's real bed.
    if (! info.model_mesh.positions.empty()) {
      const float bedBody[4] = {0.24f, 0.26f, 0.28f, 1.f};
      const auto& mesh = info.model_mesh;
      bedModel.reserve(bedModel.size() + mesh.positions.size() / 3);
      for (size_t i = 0; i + 8 < mesh.positions.size(); i += 3) {
        const simd_float3 p = simd_make_float3(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
        const simd_float3 n = simd_make_float3(mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]);
        push(bedModel, p, n, bedBody);
      }
    }
    // Bed texture quad — mapped to the bounding rect of the printable area.
    if (! info.custom_texture.empty() && info.shape.size() >= 2) {
      bedTexturePath = info.custom_texture;
      const float z = 0.02f; // slight lift so it sits above the model / z-fights nothing
      auto pushTex = [&](float x, float y, float u, float v) {
        TexturedVertex tv{{x, y, z}, {u, v}};
        bedQuad.push_back(tv);
      };
      // Two triangles, CCW seen from +Z.
      pushTex(minX, minY, 0.f, 1.f);
      pushTex(maxX, minY, 1.f, 1.f);
      pushTex(maxX, maxY, 1.f, 0.f);
      pushTex(minX, minY, 0.f, 1.f);
      pushTex(maxX, maxY, 1.f, 0.f);
      pushTex(minX, maxY, 0.f, 0.f);
    }

    if (previewMode) {
      preview = session.preview();
      for (const auto& v : preview.vertices) {
        if (v.type == Slic3r::Mobile::PreviewMoveType::Extrude) {
          bounds.add(simd_make_float3(v.position[0], v.position[1], v.position[2]));
        }
      }
    } else {
      const float body[4] = {0.55f, 0.72f, 0.62f, 1.f};
      // Amber highlight for the selected object — mirrors the desktop's
      // selection tint. Non-selected instances keep the neutral sage body.
      const float selected[4] = {1.00f, 0.72f, 0.30f, 1.f};
      // Track the dragged object's vertex range so the draw pass can slide it
      // via a shader offset without a mesh rebuild every frame. Cleared when
      // no drag is in flight.
      dragStart = 0;
      dragCount = 0;
      for (const Slic3r::Mobile::ObjectInfo& object : session.objects()) {
        const Slic3r::Mobile::MeshData mesh = session.mesh(object.id);
        const float* color = (self->_selectedId != 0 && self->_selectedId == object.id) ? selected : body;
        const size_t before = triangles.size();
        triangles.reserve(before + mesh.triangles * 3);
        for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
          const simd_float3 p = simd_make_float3(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
          const simd_float3 n = simd_make_float3(mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]);
          push(triangles, p, n, color);
          bounds.add(p);
        }
        if (self->_dragObjectId != 0 && object.id == self->_dragObjectId) {
          dragStart = before;
          dragCount = triangles.size() - before;
        }
      }
    }
  });
  if (!found) {
    // Unknown session or a slice in progress: keep what is on screen.
    return;
  }

  _bedLineVertexCount = bed.size();
  _bedLineBuffer = makeBuffer(_device, bed);
  _preview = std::move(preview);
  const bool hadBounds = _bounds.valid;
  _bounds = bounds;
  _triangleVertexCount = triangles.size();
  _dragTriStart = NSUInteger(dragStart);
  _dragTriCount = NSUInteger(dragCount);
  _triangleBuffer = triangles.empty() ? nil
                                      : [_device newBufferWithBytes:triangles.data()
                                                             length:triangles.size() * sizeof(Vertex)
                                                            options:MTLResourceStorageModeShared];
  _bedModelVertexCount = bedModel.size();
  _bedModelBuffer = bedModel.empty() ? nil
                                     : [_device newBufferWithBytes:bedModel.data()
                                                            length:bedModel.size() * sizeof(Vertex)
                                                           options:MTLResourceStorageModeShared];
  _bedTextureQuadBuffer = bedQuad.empty() ? nil
                                          : [_device newBufferWithBytes:bedQuad.data()
                                                                 length:bedQuad.size() * sizeof(TexturedVertex)
                                                                options:MTLResourceStorageModeShared];
  // Load the PNG (or JPG) bed texture. MTKTextureLoader handles both. Only
  // reload when the path changed, so a slice does not thrash the texture.
  NSString* wantedPath = bedTexturePath.empty() ? nil : [NSString stringWithUTF8String:bedTexturePath.c_str()];
  if (wantedPath == nil) {
    _bedTexture = nil;
    _bedTextureLoadedPath = nil;
  } else if (![wantedPath isEqualToString:_bedTextureLoadedPath]) {
    _bedTextureLoadedPath = wantedPath;
    if ([wantedPath.pathExtension.lowercaseString isEqualToString:@"svg"]) {
      // MTKTextureLoader does not decode SVG. Rasterize the vector art
      // through WKWebView (which handles SVG natively) and hand the
      // resulting bitmap to Metal when the snapshot returns. The STL bed
      // model draws immediately underneath, so the plate is never empty.
      _bedTexture = nil;
      [self loadSVGBedTexture:wantedPath];
    } else {
      NSURL* url = [NSURL fileURLWithPath:wantedPath];
      NSError* error = nil;
      _bedTexture = [_textureLoader newTextureWithContentsOfURL:url
                                                         options:@{ MTKTextureLoaderOptionSRGB : @NO,
                                                                    MTKTextureLoaderOptionOrigin : MTKTextureLoaderOriginBottomLeft }
                                                           error:&error];
      if (_bedTexture == nil) {
        NSLog(@"OrcaViewport: bed texture load failed for %@: %@", wantedPath, error);
      }
    }
  }
  [self rebuildToolpath];
  if (!hadBounds) {
    [self fit];
  }
  [_view setNeedsDisplay];
}

- (void)rebuildToolpath {
  std::vector<Segment> extrusions;
  std::vector<Segment> markers;
  _extrusionMoves.clear();
  _markerMoves.clear();
  if (_mode == OrcaViewportModePreview && _preview.vertices.size() > 1) {
    extrusions.reserve(_preview.vertices.size());
    _extrusionMoves.reserve(_preview.vertices.size());
    // Precompute the max feedrate / volumetric rate / layer time so the
    // gradient spans a stable range regardless of how fast, thick or slow
    // the print is.
    float maxFeedrate = 0.f;
    float maxVolumetric = 0.f;
    float maxLayerTime = 0.f;
    if (_colorMode == OrcaPreviewColorModeSpeed || _colorMode == OrcaPreviewColorModeFlow) {
      for (const auto& v : _preview.vertices) {
        if (v.type != Slic3r::Mobile::PreviewMoveType::Extrude) continue;
        maxFeedrate   = std::max(maxFeedrate,   v.feedrate);
        maxVolumetric = std::max(maxVolumetric, v.volumetric_rate);
      }
    } else if (_colorMode == OrcaPreviewColorModeLayerTime) {
      for (float t : _preview.layer_times) maxLayerTime = std::max(maxLayerTime, t);
    }
    auto colorForRate = [](float value, float maxValue, float out[4]) {
      const float t = maxValue > 0 ? std::clamp(value / maxValue, 0.f, 1.f) : 0.f;
      // Simple three-stop gradient: blue → green → red.
      if (t < 0.5f) {
        const float k = t / 0.5f;
        out[0] = 0.f;  out[1] = k;         out[2] = 1.f - k;
      } else {
        const float k = (t - 0.5f) / 0.5f;
        out[0] = k;    out[1] = 1.f - k;   out[2] = 0.f;
      }
      out[3] = 1.f;
    };
    for (size_t i = 1; i < _preview.vertices.size(); ++i) {
      const Slic3r::Mobile::PreviewVertex& a = _preview.vertices[i - 1];
      const Slic3r::Mobile::PreviewVertex& b = _preview.vertices[i];
      const float* color = nullptr;
      float dynamic[4];
      if (b.type == Slic3r::Mobile::PreviewMoveType::Extrude) {
        switch (_colorMode) {
          case OrcaPreviewColorModeSpeed:
            colorForRate(b.feedrate, maxFeedrate, dynamic);
            color = dynamic;
            break;
          case OrcaPreviewColorModeFlow:
            colorForRate(b.volumetric_rate, maxVolumetric, dynamic);
            color = dynamic;
            break;
          case OrcaPreviewColorModeTool: {
            const size_t idx = b.extruder_id;
            if (idx < _preview.tool_colors.size()) {
              const auto& c = _preview.tool_colors[idx];
              dynamic[0] = c[0]; dynamic[1] = c[1]; dynamic[2] = c[2]; dynamic[3] = c[3];
              color = dynamic;
            } else {
              color = kRoleColors[static_cast<size_t>(b.role)];
            }
            break;
          }
          case OrcaPreviewColorModeLayerTime: {
            const float t = (b.layer_id < _preview.layer_times.size()) ? _preview.layer_times[b.layer_id] : 0.f;
            colorForRate(t, maxLayerTime, dynamic);
            color = dynamic;
            break;
          }
          case OrcaPreviewColorModeFeature:
          default:
            color = kRoleColors[std::min(static_cast<size_t>(b.role), kTravelColor - 1)];
            break;
        }
        pushSegment(extrusions, _extrusionMoves, i, a.position, b.position, color);
      } else if (_showTravels && b.type == Slic3r::Mobile::PreviewMoveType::Travel) {
        pushSegment(markers, _markerMoves, i, a.position, b.position, kRoleColors[kTravelColor]);
      } else if (_showWipes && b.type == Slic3r::Mobile::PreviewMoveType::Wipe) {
        static const float wipe[4] = {0.90f, 0.70f, 0.10f, 1.f};
        pushSegment(markers, _markerMoves, i, a.position, b.position, wipe);
      } else if (_showRetractions && (b.type == Slic3r::Mobile::PreviewMoveType::Retract
                                      || b.type == Slic3r::Mobile::PreviewMoveType::Unretract)) {
        // Draw a short vertical tick at the position as a stand-in marker.
        static const float retract[4] = {0.95f, 0.20f, 0.20f, 1.f};
        const float top[3] = {b.position[0], b.position[1], b.position[2] + 0.6f};
        pushSegment(markers, _markerMoves, i, b.position, top, retract);
      }
    }
  }
  _extrusionBuffer = makeBuffer(_device, extrusions);
  _markerBuffer = makeBuffer(_device, markers);
  [self updateToolpathRange];
}

// Narrows the drawn toolpath to the layer and time filters without touching the
// buffers. Moves come in G-code order, where layer ids and print time only grow, so
// each filter is a prefix or suffix of the moves and a binary search finds it.
- (void)updateToolpathRange {
  const auto& moves = _preview.vertices;
  size_t lo = 0;
  size_t hi = moves.size();
  if (_minLayer >= 0) {
    const unsigned int minLayer = static_cast<unsigned int>(_minLayer);
    lo = std::partition_point(moves.begin(), moves.end(), [&](const auto& v) { return v.layer_id < minLayer; }) - moves.begin();
  }
  if (_maxLayer >= 0) {
    const unsigned int maxLayer = static_cast<unsigned int>(_maxLayer);
    hi = std::partition_point(moves.begin(), moves.end(), [&](const auto& v) { return v.layer_id <= maxLayer; }) - moves.begin();
  }
  if (_maxTime >= 0.f) {
    const float maxTime = _maxTime;
    hi = std::min<size_t>(hi, std::partition_point(moves.begin(), moves.end(), [&](const auto& v) { return v.time <= maxTime; }) - moves.begin());
  }
  auto range = [&](const std::vector<uint32_t>& segmentMoves) {
    const size_t first = std::lower_bound(segmentMoves.begin(), segmentMoves.end(), lo) - segmentMoves.begin();
    const size_t last = std::lower_bound(segmentMoves.begin(), segmentMoves.end(), std::max(lo, hi)) - segmentMoves.begin();
    return NSMakeRange(first, last - first);
  };
  _extrusionRange = range(_extrusionMoves);
  _markerRange = range(_markerMoves);
  [_view setNeedsDisplay];
}

#pragma mark - Camera

- (void)fit {
  _target = _bounds.valid ? _bounds.center() : simd_make_float3(0, 0, 0);
  const float radius = std::max(_bounds.radius(), 20.f);
  _distance = radius / sinf(0.35f) * 1.15f;
  [_view setNeedsDisplay];
}

- (void)orbitBy:(CGPoint)delta {
  _yaw += float(delta.x) * 0.01f;
  _pitch = std::clamp(_pitch + float(delta.y) * 0.01f, -1.5f, 1.5f);
  [_view setNeedsDisplay];
}

- (void)panBy:(CGPoint)delta {
  // Move the target in the camera's screen plane, scaled so a drag follows the finger.
  const float scale = _distance * 0.0015f;
  const simd_float3 forward = simd_normalize(simd_make_float3(sinf(_yaw) * cosf(_pitch), cosf(_yaw) * cosf(_pitch), sinf(_pitch)));
  const simd_float3 right = simd_normalize(simd_cross(forward, simd_make_float3(0, 0, 1)));
  const simd_float3 upv = simd_cross(right, forward);
  _target = _target - right * (float(delta.x) * scale) + upv * (float(delta.y) * scale);
  [_view setNeedsDisplay];
}

- (void)zoomBy:(CGFloat)factor {
  if (factor > 0) {
    _distance = std::clamp(_distance / float(factor), 5.f, 5000.f);
    [_view setNeedsDisplay];
  }
}

- (simd_float4x4)viewProjectionForSize:(CGSize)size {
  const simd_float3 offset = simd_make_float3(sinf(_yaw) * cosf(_pitch), cosf(_yaw) * cosf(_pitch), sinf(_pitch)) * _distance;
  const simd_float3 eye = _target + offset;
  const simd_float4x4 view = lookAt(eye, _target, simd_make_float3(0, 0, 1));
  const float aspect = size.height > 0 ? float(size.width / size.height) : 1.f;
  const simd_float4x4 projection = perspective(0.7f, aspect, std::max(_distance * 0.01f, 0.1f), _distance * 10.f + 1000.f);
  return simd_mul(projection, view);
}

- (void)screenToRayFromX:(CGFloat)x
                       y:(CGFloat)y
               originOut:(vector_float3*)originOut
            directionOut:(vector_float3*)directionOut {
  MTKView* view = _view;
  if (view == nil || originOut == NULL || directionOut == NULL) return;
  // Points → NDC ([-1, 1], y-up in NDC vs y-down for touches).
  const CGSize sz = view.bounds.size;
  if (sz.width <= 0 || sz.height <= 0) return;
  const float nx = float(x / sz.width)  * 2.f - 1.f;
  const float ny = 1.f - float(y / sz.height) * 2.f;

  const simd_float4x4 vp = [self viewProjectionForSize:sz];
  const simd_float4x4 invVP = simd_inverse(vp);
  simd_float4 nearP = simd_mul(invVP, simd_make_float4(nx, ny, 0.f, 1.f));
  simd_float4 farP  = simd_mul(invVP, simd_make_float4(nx, ny, 1.f, 1.f));
  nearP /= nearP.w;
  farP  /= farP.w;
  const simd_float3 origin    = simd_make_float3(nearP.x, nearP.y, nearP.z);
  const simd_float3 direction = simd_normalize(simd_make_float3(farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z));
  *originOut = origin;
  *directionOut = direction;
}

- (BOOL)screenToPlateXYFromX:(CGFloat)x y:(CGFloat)y outX:(CGFloat*)outX outY:(CGFloat*)outY {
  if (outX == NULL || outY == NULL) return NO;
  simd_float3 origin;
  simd_float3 direction;
  [self screenToRayFromX:x y:y originOut:&origin directionOut:&direction];
  // Ray hits Z=0 where origin.z + t * direction.z == 0. Reject grazing angles;
  // they'd project the finger to points miles from the plate.
  if (fabsf(direction.z) < 1e-4f) return NO;
  const float t = -origin.z / direction.z;
  if (t <= 0.f) return NO;
  *outX = CGFloat(origin.x + direction.x * t);
  *outY = CGFloat(origin.y + direction.y * t);
  return YES;
}

- (uint64_t)pickObjectAtScreenX:(CGFloat)x y:(CGFloat)y {
  simd_float3 origin;
  simd_float3 direction;
  [self screenToRayFromX:x y:y originOut:&origin directionOut:&direction];
  uint64_t hitId = 0;
  margelo::nitro::orca::SessionRegistry::with(_sessionId, [&](Slic3r::Mobile::Session& session) {
    const Slic3r::Mobile::Session::PickHit hit = session.pick_at(
      { double(origin.x), double(origin.y), double(origin.z) },
      { double(direction.x), double(direction.y), double(direction.z) });
    hitId = static_cast<uint64_t>(hit.id);
  });
  return hitId;
}

- (void)setObjectTool:(NSInteger)tool {
  // Ignore mid-gesture changes — the drag in progress keeps its current tool
  // until release. Changes only affect the NEXT drag.
  if (_dragObjectId == 0) {
    _dragTool = tool;
  }
}

- (BOOL)beginDragObject:(uint64_t)objectId atScreenX:(CGFloat)x y:(CGFloat)y {
  if (objectId == 0) return NO;
  CGFloat anchorX = 0, anchorY = 0;
  if (![self screenToPlateXYFromX:x y:y outX:&anchorX outY:&anchorY]) return NO;

  BOOL found = NO;
  simd_float3 initialPos = simd_make_float3(0, 0, 0);
  simd_float3 initialRot = simd_make_float3(0, 0, 0);
  simd_float3 initialSca = simd_make_float3(1, 1, 1);
  margelo::nitro::orca::SessionRegistry::with(_sessionId, [&](Slic3r::Mobile::Session& session) {
    for (const Slic3r::Mobile::ObjectInfo& obj : session.objects()) {
      if (obj.id != objectId) continue;
      initialPos = simd_make_float3(float(obj.position[0]), float(obj.position[1]), float(obj.position[2]));
      initialRot = simd_make_float3(float(obj.rotation[0]), float(obj.rotation[1]), float(obj.rotation[2]));
      initialSca = simd_make_float3(float(obj.scale[0]),    float(obj.scale[1]),    float(obj.scale[2]));
      found = YES;
      break;
    }
  });
  if (!found) return NO;

  _dragObjectId        = objectId;
  _dragInitialPosition = initialPos;
  _dragInitialRotation = initialRot;
  _dragInitialScale    = initialSca;
  _dragAnchorXY        = simd_make_float2(float(anchorX), float(anchorY));
  _dragStartScreen     = CGPointMake(x, y);
  _dragOffset          = simd_make_float3(0, 0, 0);
  _dragRotationZ       = 0.f;
  _dragScaleFactor     = 1.f;
  // Reload once so the triangle buffer records this object's vertex range in
  // `_dragTriStart` / `_dragTriCount`. Subsequent `dragToScreenX:` updates only
  // the uniform + setNeedsDisplay — no per-frame mesh rebuild.
  [self reload];
  return YES;
}

- (void)dragToScreenX:(CGFloat)x y:(CGFloat)y {
  if (_dragObjectId == 0) return;
  switch (_dragTool) {
    case 1: {
      // Rotate: horizontal finger travel controls angle. Full screen width ≈ π
      // rad — sensitive enough for fine adjustments, coarse enough to flip 180°.
      const CGFloat dx = x - _dragStartScreen.x;
      const CGFloat viewWidth = _view != nil ? _view.bounds.size.width : 400;
      const float pxPerPi = viewWidth > 0 ? float(viewWidth) : 400.f;
      _dragRotationZ = float(dx) / pxPerPi * float(M_PI);
      break;
    }
    case 2: {
      // Scale: vertical finger travel controls uniform scale, up = grow, down =
      // shrink. Clamp to a reasonable range so a stray drag can't zero the mesh.
      const CGFloat dy = _dragStartScreen.y - y; // positive when finger moved up
      const CGFloat viewHeight = _view != nil ? _view.bounds.size.height : 400;
      const float halfPx = viewHeight > 0 ? float(viewHeight) * 0.5f : 200.f;
      const float exponent = float(dy) / halfPx; // ±1 across half the view
      _dragScaleFactor = std::clamp(powf(2.f, exponent), 0.1f, 10.f);
      break;
    }
    default: {
      // Move: keep the finger's initial plate anchor sticking to the finger.
      CGFloat plateX = 0, plateY = 0;
      if (![self screenToPlateXYFromX:x y:y outX:&plateX outY:&plateY]) return;
      _dragOffset = simd_make_float3(
        float(plateX) - _dragAnchorXY.x,
        float(plateY) - _dragAnchorXY.y,
        0.f);
      break;
    }
  }
  [_view setNeedsDisplay];
}

- (void)endDrag {
  if (_dragObjectId == 0) return;
  const Slic3r::Mobile::Vec3 newPos = {
    double(_dragInitialPosition.x + _dragOffset.x),
    double(_dragInitialPosition.y + _dragOffset.y),
    double(_dragInitialPosition.z + _dragOffset.z),
  };
  const Slic3r::Mobile::Vec3 rot = {
    double(_dragInitialRotation.x),
    double(_dragInitialRotation.y),
    double(_dragInitialRotation.z + _dragRotationZ),
  };
  const Slic3r::Mobile::Vec3 sca = {
    double(_dragInitialScale.x * _dragScaleFactor),
    double(_dragInitialScale.y * _dragScaleFactor),
    double(_dragInitialScale.z * _dragScaleFactor),
  };
  const uint64_t objectId = _dragObjectId;
  const NSInteger tool = _dragTool;
  margelo::nitro::orca::SessionRegistry::with(_sessionId, [&](Slic3r::Mobile::Session& session) {
    session.set_transform(static_cast<unsigned long>(objectId), newPos, rot, sca);
    const char* label = tool == 1 ? "Rotate" : tool == 2 ? "Scale" : "Move object";
    session.take_snapshot(label);
  });
  _dragObjectId    = 0;
  _dragOffset      = simd_make_float3(0, 0, 0);
  _dragRotationZ   = 0.f;
  _dragScaleFactor = 1.f;
  _dragTriStart    = 0;
  _dragTriCount    = 0;
  [self reload];
}

- (BOOL)isDragging {
  return _dragObjectId != 0;
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  [view setNeedsDisplay];
}

- (void)drawInMTKView:(MTKView*)view {
  if (_pipeline == nil) {
    return;
  }
  MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
  id<CAMetalDrawable> drawable = view.currentDrawable;
  if (pass == nil || drawable == nil) {
    return;
  }

  Uniforms uniforms;
  uniforms.mvp = [self viewProjectionForSize:view.drawableSize];
  uniforms.lightDir = simd_make_float4(0.3f, -0.5f, 0.8f, 0);
  uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
  uniforms.dragAnchor = simd_make_float4(0, 0, 0, 0);
  uniforms.dragParams = simd_make_float4(0, 1, 0, 0); // no rotation, identity scale

  id<MTLCommandBuffer> commands = [_queue commandBuffer];
  id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
  [encoder setDepthStencilState:_depthState];
  [encoder setCullMode:MTLCullModeNone];

  // Custom bed model (STL) first — it sits under everything.
  if (_bedModelBuffer != nil) {
    uniforms.params = simd_make_float4(1, 0, 0, 0);
    uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
    [encoder setRenderPipelineState:_pipeline];
    [encoder setVertexBuffer:_bedModelBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_bedModelVertexCount];
  }
  // Bed texture quad — draws on top of the STL / directly on the polygon.
  if (_bedTextureQuadBuffer != nil && _bedTexture != nil && _bedTexturePipeline != nil) {
    uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
    [encoder setRenderPipelineState:_bedTexturePipeline];
    [encoder setVertexBuffer:_bedTextureQuadBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:_bedTexture atIndex:0];
    [encoder setFragmentSamplerState:_bedSampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
  }

  uniforms.params = simd_make_float4(0, 0, 0, 0);
  uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
  [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
  if (_segmentLinePipeline != nil) {
    // One instance per segment; the buffer offset selects the filtered range.
    [encoder setRenderPipelineState:_segmentLinePipeline];
    for (const auto& [buffer, range] : {std::pair{_extrusionBuffer, _extrusionRange}, std::pair{_markerBuffer, _markerRange}}) {
      if (buffer == nil || range.length == 0) continue;
      [encoder setVertexBuffer:buffer offset:range.location * sizeof(Segment) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:2 instanceCount:range.length];
    }
  }
  [encoder setRenderPipelineState:_pipeline];
  if (_bedLineBuffer != nil) {
    [encoder setVertexBuffer:_bedLineBuffer offset:0 atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:_bedLineVertexCount];
  }
  if (_triangleBuffer != nil) {
    uniforms.params = simd_make_float4(1, 0, 0, 0);
    // While an object is being dragged, split the triangle draw so we can slide
    // the dragged vertices by a shader offset without rebuilding geometry each
    // frame. Non-dragged vertices before/after the range render normally; the
    // middle slice picks up the current drag delta as an added translation.
    const bool splitDrag = (_dragObjectId != 0 && _dragTriCount > 0 && _dragTriStart + _dragTriCount <= _triangleVertexCount);
    if (splitDrag) {
      [encoder setVertexBuffer:_triangleBuffer offset:0 atIndex:0];
      if (_dragTriStart > 0) {
        uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
        uniforms.dragAnchor = simd_make_float4(0, 0, 0, 0);
        uniforms.dragParams = simd_make_float4(0, 1, 0, 0);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_dragTriStart];
      }
      // The dragged range picks up per-frame rotation + scale from
      // `_dragRotationZ` / `_dragScaleFactor`, applied around the object's
      // instance origin (`_dragInitialPosition`). Move tool leaves those at
      // (0, 1) so only `modelOffset` acts.
      uniforms.modelOffset = simd_make_float4(_dragOffset.x, _dragOffset.y, _dragOffset.z, 0);
      uniforms.dragAnchor  = simd_make_float4(_dragInitialPosition.x, _dragInitialPosition.y, _dragInitialPosition.z, 0);
      uniforms.dragParams  = simd_make_float4(_dragRotationZ, _dragScaleFactor, 0, 0);
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:_dragTriStart vertexCount:_dragTriCount];
      const NSUInteger tail = _dragTriStart + _dragTriCount;
      if (tail < _triangleVertexCount) {
        uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
        uniforms.dragAnchor  = simd_make_float4(0, 0, 0, 0);
        uniforms.dragParams  = simd_make_float4(0, 1, 0, 0);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:tail vertexCount:(_triangleVertexCount - tail)];
      }
    } else {
      uniforms.modelOffset = simd_make_float4(0, 0, 0, 0);
      uniforms.dragAnchor  = simd_make_float4(0, 0, 0, 0);
      uniforms.dragParams  = simd_make_float4(0, 1, 0, 0);
      [encoder setVertexBuffer:_triangleBuffer offset:0 atIndex:0];
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_triangleVertexCount];
    }
  }

  [encoder endEncoding];
  [commands presentDrawable:drawable];
  [commands commit];
}

#pragma mark - SVG bed texture rasterization

- (void)loadSVGBedTexture:(NSString*)path {
  // Vendor bed textures (Qidi, Prusa, Voron, …) are .svg files that
  // MTKTextureLoader cannot decode. WKWebView loads SVG natively, so we
  // render the file into an offscreen 1024×1024 web view and hand the
  // resulting snapshot back to Metal as a texture. Must run on the main
  // thread — WKWebView is UIKit.
  const NSString* wanted = [path copy];
  dispatch_async(dispatch_get_main_queue(), ^{
    NSString* svg;
    NSError* readError = nil;
    svg = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&readError];
    if (svg == nil || readError != nil) {
      NSLog(@"OrcaViewport: cannot read SVG bed texture %@: %@", path, readError);
      return;
    }
    NSString* html = [NSString stringWithFormat:
      @"<!doctype html><html><head><meta name=viewport content='width=1024'/><style>"
      @"html,body{margin:0;padding:0;background:transparent;width:1024px;height:1024px;overflow:hidden}"
      @"svg{width:100%%;height:100%%;display:block}"
      @"</style></head><body>%@</body></html>", svg];

    const CGRect frame = CGRectMake(0, 0, 1024, 1024);
    WKWebViewConfiguration* cfg = [[WKWebViewConfiguration alloc] init];
    self->_svgRasterWebView = [[WKWebView alloc] initWithFrame:frame configuration:cfg];
    self->_svgRasterWebView.opaque = NO;
    self->_svgRasterWebView.backgroundColor = [UIColor clearColor];
    self->_svgRasterWebView.scrollView.backgroundColor = [UIColor clearColor];
    self->_svgRasterWebView.navigationDelegate = self;
    // Track the path this render is for so a race between two loads keeps
    // the latest result.
    objc_setAssociatedObject(self->_svgRasterWebView, "orca.svg.path", wanted, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [self->_svgRasterWebView loadHTMLString:html baseURL:[NSURL fileURLWithPath:path.stringByDeletingLastPathComponent isDirectory:YES]];
  });
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  // Wait one runloop so the layout / paint pass has landed before snapshotting.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.05 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    WKSnapshotConfiguration* snap = [[WKSnapshotConfiguration alloc] init];
    snap.snapshotWidth = @1024;
    [webView takeSnapshotWithConfiguration:snap completionHandler:^(UIImage* image, NSError* error) {
      NSString* svgPath = objc_getAssociatedObject(webView, "orca.svg.path");
      if (image == nil) {
        NSLog(@"OrcaViewport: SVG snapshot failed for %@: %@", svgPath, error);
        if (self->_svgRasterWebView == webView) self->_svgRasterWebView = nil;
        return;
      }
      CGImageRef cg = image.CGImage;
      if (cg != NULL) {
        NSError* texError = nil;
        id<MTLTexture> tex = [self->_textureLoader newTextureWithCGImage:cg
                                                                  options:@{ MTKTextureLoaderOptionSRGB : @NO,
                                                                             MTKTextureLoaderOptionOrigin : MTKTextureLoaderOriginBottomLeft }
                                                                    error:&texError];
        if (tex != nil && [svgPath isEqualToString:self->_bedTextureLoadedPath]) {
          self->_bedTexture = tex;
          [self->_view setNeedsDisplay];
        } else if (tex == nil) {
          NSLog(@"OrcaViewport: MTL texture from SVG failed for %@: %@", svgPath, texError);
        }
      }
      if (self->_svgRasterWebView == webView) self->_svgRasterWebView = nil;
    }];
  });
}

@end
