import type { HybridView, HybridViewMethods, HybridViewProps } from 'react-native-nitro-modules'

// The 3D viewport. A Metal view on iOS that reads geometry straight from the native
// session (found by id), so no mesh or toolpath data crosses the JavaScript boundary.

export type ViewportMode = 'scene' | 'preview'

/** World-space ray computed from a screen point. */
export interface PickRay {
  originX: number
  originY: number
  originZ: number
  directionX: number
  directionY: number
  directionZ: number
}

export type PreviewColorMode = 'feature' | 'speed' | 'flow' | 'layerTime' | 'tool'

/**
 * What a 1-finger drag on the selected object does when the finger begins on
 * that object. Move slides it across the plate, Rotate spins it around Z, and
 * Scale grows/shrinks uniformly. Drags that don't start on the selected object
 * always orbit the camera, regardless of tool.
 */
export type ObjectTool = 'move' | 'rotate' | 'scale'

export interface OrcaViewportProps extends HybridViewProps {
  /** OrcaSession.id of the session to draw. 0 draws only the bed. */
  sessionId: number
  /** 'scene' draws the objects on the bed, 'preview' the toolpath of the last export. */
  mode: ViewportMode
  /** Lowest layer id to draw in preview mode; -1 starts at layer 0. */
  minLayer: number
  /** Highest layer id to draw in preview mode; -1 draws every layer. */
  maxLayer: number
  /** Highest print time (seconds from start) to draw in preview; -1 draws every move. */
  maxTime: number
  /** Draw travel moves in preview mode. */
  showTravels: boolean
  /** Draw retraction / unretraction markers in preview mode. */
  showRetractions: boolean
  /** Draw wipe moves in preview mode. */
  showWipes: boolean
  /** How to color the extrusions in preview mode. */
  colorMode: PreviewColorMode
  /** Bump to re-read geometry from the session after the model or the print changed. */
  revision: number
  /**
   * Fired on a short single tap on the viewport. x, y are in the view's local
   * coordinate space (points, top-left origin) — the same space `screenToRay`
   * expects. The recognizer runs simultaneously with the native pan / pinch /
   * orbit so this does not block camera manipulation the way an RN overlay
   * Pressable would.
   */
  onTap?: (x: number, y: number) => void
}

export interface OrcaViewportMethods extends HybridViewMethods {
  /** Frames the bed and everything on it. */
  fit(): void
  /**
   * Converts a point in the view's local coordinate space (points, top-left
   * origin, matches touch events) to a world-space ray suitable for
   * `OrcaSession.pickAt`. Handy when JS wants to run its own picking flow
   * against a Pressable wrapping the viewport.
   */
  screenToRay(x: number, y: number): PickRay
  /**
   * Highlights the object with the given id with a warm tint. Pass 0 to
   * clear the selection.
   */
  setSelectedId(id: number): void
  /**
   * Chooses the interaction for a 1-finger drag that lands on the selected
   * object. Default is 'move'. Changing tool mid-gesture is a no-op; the
   * gesture in progress keeps its current tool until release.
   */
  setObjectTool(tool: ObjectTool): void
}

// iOS only for now; the Android (OpenGL ES) view comes with the Android port.
export type OrcaViewport = HybridView<OrcaViewportProps, OrcaViewportMethods, { ios: 'swift' }>
