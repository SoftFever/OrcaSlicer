import { getHostComponent, type HybridRef } from 'react-native-nitro-modules'

import OrcaViewportConfig from '../../nitrogen/generated/shared/json/OrcaViewportConfig.json'
import type { OrcaViewportMethods, OrcaViewportProps } from '../specs/OrcaViewport.nitro'

/** The 3D viewport, rendered natively (Metal on iOS). */
export const OrcaViewport = getHostComponent<OrcaViewportProps, OrcaViewportMethods>(
  'OrcaViewport',
  () => OrcaViewportConfig,
)

export type OrcaViewportRef = HybridRef<OrcaViewportProps, OrcaViewportMethods>
