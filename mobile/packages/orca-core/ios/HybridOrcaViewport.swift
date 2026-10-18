import Foundation
import MetalKit
import NitroModules
import UIKit

/// The MTKView with the camera gestures. Gesture targets must be NSObjects, which the
/// Nitro spec base class is not, so the view owns them.
final class OrcaViewportView: MTKView, UIGestureRecognizerDelegate {
  var renderer: OrcaViewportRenderer?
  /// Called on a short single tap. Coords are in this view's local point space.
  /// The parent Nitro view forwards it to JS through the `onTap` callback prop.
  var onSingleTap: ((CGPoint) -> Void)?
  private var lastPinchScale: CGFloat = 1

  override init(frame: CGRect, device: MTLDevice?) {
    super.init(frame: frame, device: device)
    isOpaque = true
    let orbit = UIPanGestureRecognizer(target: self, action: #selector(onOrbit(_:)))
    orbit.maximumNumberOfTouches = 1
    // Delegate returning true for `shouldRecognizeSimultaneouslyWith` on orbit
    // lets it run alongside the singleTap. Without this, iOS's default arbitration
    // makes tap and pan mutually exclusive on the same view — and orbit-pan's
    // touch tracking begins on touchDown, starving the tap of its recognition.
    orbit.delegate = self
    addGestureRecognizer(orbit)
    let pan = UIPanGestureRecognizer(target: self, action: #selector(onPan(_:)))
    pan.minimumNumberOfTouches = 2
    pan.maximumNumberOfTouches = 2
    pan.delegate = self
    addGestureRecognizer(pan)
    let pinch = UIPinchGestureRecognizer(target: self, action: #selector(onPinch(_:)))
    pinch.delegate = self
    addGestureRecognizer(pinch)
    let doubleTap = UITapGestureRecognizer(target: self, action: #selector(onDoubleTap(_:)))
    doubleTap.numberOfTapsRequired = 2
    doubleTap.delegate = self
    addGestureRecognizer(doubleTap)
    // Single tap for object picking. Attached to the Metal view directly so it
    // coexists with the pan / pinch above without an RN Pressable overlay
    // starving them of touches, and defers to double-tap so a two-tap fit
    // gesture does not also fire a pick.
    let singleTap = UITapGestureRecognizer(target: self, action: #selector(onSingleTapGesture(_:)))
    singleTap.numberOfTapsRequired = 1
    singleTap.require(toFail: doubleTap)
    // A pan gesture that never crosses the movement threshold cancels itself
    // and lets tap fire. But if the tap does not `cancelsTouchesInView`, the
    // orbit-pan keeps seeing the touch even after we recognized. Cleaner to
    // opt out of both interference paths explicitly.
    singleTap.cancelsTouchesInView = false
    singleTap.delegate = self
    addGestureRecognizer(singleTap)
  }

  // MARK: UIGestureRecognizerDelegate
  //
  // Two concerns interact here:
  //  1. Our own recognizers (orbit / two-finger pan / pinch / tap) must be
  //     free to co-recognize on this view — that's what rescues single-tap
  //     selection from being pre-empted while orbit-pan is still `.possible`.
  //  2. When this view is inside an RN ScrollView, the ancestor's pan must
  //     win over ours so scrolling the surrounding UI doesn't also drag the
  //     model. Simultaneous recognition would let both fire.

  func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                         shouldRecognizeSimultaneouslyWith otherGestureRecognizer: UIGestureRecognizer) -> Bool {
    // Only when the other recognizer belongs to this same view. Ancestors
    // (e.g. UIScrollView) are excluded — see `shouldBeRequiredToFailBy` below.
    return otherGestureRecognizer.view === self
  }

  func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                         shouldBeRequiredToFailBy otherGestureRecognizer: UIGestureRecognizer) -> Bool {
    // Yield to an ancestor UIScrollView's own pan: if the parent scroll starts
    // recognizing (finger moved along its scrollable axis), our orbit / drag
    // stays `.possible` and eventually fails, so scrolling the outer UI does
    // not also spin the object underneath.
    guard otherGestureRecognizer is UIPanGestureRecognizer,
          let other = otherGestureRecognizer.view as? UIScrollView else { return false }
    var v: UIView? = self.superview
    while let cur = v {
      if cur === other { return true }
      v = cur.superview
    }
    return false
  }

  required init(coder: NSCoder) {
    fatalError("init(coder:) is not supported")
  }

  @objc private func onOrbit(_ gesture: UIPanGestureRecognizer) {
    // Two roles for a 1-finger pan: orbit the camera, or drag the selected
    // object across the plate. We decide at `.began` by ray-casting from the
    // initial finger location; if the object under the finger matches the
    // current selection, we enter drag mode and every `.changed` moves the
    // object instead of the camera. Snapshot happens once on release.
    guard let renderer = renderer else { return }
    switch gesture.state {
    case .began:
      let location = gesture.location(in: self)
      let selectedId = renderer.selectedId
      var startedDrag = false
      if selectedId != 0 {
        let touched = renderer.pickObject(atScreenX: location.x, y: location.y)
        if touched == selectedId {
          startedDrag = renderer.beginDragObject(selectedId, atScreenX: location.x, y: location.y)
        }
      }
      if !startedDrag {
        // Fall through to orbit; the delta since `.began` is zero here so
        // there's nothing to apply yet.
        gesture.setTranslation(.zero, in: self)
      }
    case .changed:
      if renderer.isDragging {
        let location = gesture.location(in: self)
        renderer.drag(toScreenX: location.x, y: location.y)
      } else {
        let delta = gesture.translation(in: self)
        renderer.orbit(by: delta)
        gesture.setTranslation(.zero, in: self)
      }
    case .ended, .cancelled, .failed:
      if renderer.isDragging {
        renderer.endDrag()
      }
    default:
      break
    }
  }

  @objc private func onPan(_ gesture: UIPanGestureRecognizer) {
    let delta = gesture.translation(in: self)
    renderer?.pan(by: delta)
    gesture.setTranslation(.zero, in: self)
  }

  @objc private func onPinch(_ gesture: UIPinchGestureRecognizer) {
    if gesture.state == .began {
      lastPinchScale = 1
    }
    renderer?.zoom(by: gesture.scale / lastPinchScale)
    lastPinchScale = gesture.scale
  }

  @objc private func onDoubleTap(_ gesture: UITapGestureRecognizer) {
    renderer?.fit()
  }

  @objc private func onSingleTapGesture(_ gesture: UITapGestureRecognizer) {
    // TEMP DIAG: log every time the gesture recognizer transitions, so we can
    // tell whether taps are reaching the metal view at all.
    NSLog("[Orca] onSingleTapGesture state=%ld closureIsNil=%d",
          Int(gesture.state.rawValue), onSingleTap == nil ? 1 : 0)
    guard gesture.state == .ended else { return }
    onSingleTap?(gesture.location(in: self))
  }
}

/// The `OrcaViewport` Nitro View. Props arrive from React; geometry comes from the native
/// session the `sessionId` names, read by the renderer, so nothing crosses the JS bridge.
class HybridOrcaViewport: HybridOrcaViewportSpec {
  private let metalView = OrcaViewportView(frame: .zero, device: nil)
  private let renderer: OrcaViewportRenderer

  var view: UIView { metalView }

  var sessionId: Double = 0 {
    didSet {
      renderer.sessionId = sessionId > 0 ? UInt64(sessionId) : 0
      renderer.reload()
    }
  }

  var mode: ViewportMode = .scene {
    didSet {
      renderer.mode = mode == .preview ? .preview : .scene
      renderer.reload()
    }
  }

  var minLayer: Double = -1 {
    didSet { renderer.minLayer = Int(minLayer) }
  }

  var maxLayer: Double = -1 {
    didSet { renderer.maxLayer = Int(maxLayer) }
  }

  var maxTime: Double = -1 {
    didSet { renderer.maxTime = Float(maxTime) }
  }

  var showTravels: Bool = false {
    didSet { renderer.showTravels = showTravels }
  }

  var showRetractions: Bool = false {
    didSet { renderer.showRetractions = showRetractions }
  }

  var showWipes: Bool = false {
    didSet { renderer.showWipes = showWipes }
  }

  var colorMode: PreviewColorMode = .feature {
    didSet {
      switch colorMode {
      case .feature:   renderer.colorMode = .feature
      case .speed:     renderer.colorMode = .speed
      case .flow:      renderer.colorMode = .flow
      case .layertime: renderer.colorMode = .layerTime
      case .tool:      renderer.colorMode = .tool
      }
    }
  }

  var revision: Double = 0 {
    didSet { renderer.reload() }
  }

  // Nitrogen generates the callback prop declaration in HybridOrcaViewportSpec; the
  // concrete subclass holds the stored value and the metalView forwards taps into it.
  var onTap: ((_ x: Double, _ y: Double) -> Void)? {
    // TEMP DIAG: log every time Nitro's Fabric setter writes onTap so we can
    // tell whether JS ever reaches this closure. Remove once picking is verified.
    didSet { NSLog("[Orca] HybridOrcaViewport.onTap set; hasValue=%d", onTap != nil ? 1 : 0) }
  }

  override init() {
    renderer = OrcaViewportRenderer(view: metalView)
    super.init()
    metalView.renderer = renderer
    metalView.onSingleTap = { [weak self] point in
      // TEMP DIAG: shows whether the singleTap recognizer fired and whether an
      // onTap callback is currently installed to forward it to JS.
      NSLog("[Orca] onSingleTap forward: point=(%.1f,%.1f) onTapIsNil=%d",
            Double(point.x), Double(point.y), self?.onTap == nil ? 1 : 0)
      self?.onTap?(Double(point.x), Double(point.y))
    }
  }

  func fit() throws {
    renderer.fit()
  }

  func screenToRay(x: Double, y: Double) throws -> PickRay {
    var origin = simd_float3(0, 0, 0)
    var direction = simd_float3(0, 0, -1)
    renderer.screenToRayFrom(x: CGFloat(x), y: CGFloat(y), originOut: &origin, directionOut: &direction)
    return PickRay(
      originX: Double(origin.x),
      originY: Double(origin.y),
      originZ: Double(origin.z),
      directionX: Double(direction.x),
      directionY: Double(direction.y),
      directionZ: Double(direction.z),
    )
  }

  func setSelectedId(id: Double) throws {
    renderer.selectedId = id > 0 ? UInt64(id) : 0
  }

  func setObjectTool(tool: ObjectTool) throws {
    switch tool {
    case .move:   renderer.setObjectTool(0)
    case .rotate: renderer.setObjectTool(1)
    case .scale:  renderer.setObjectTool(2)
    }
  }
}
