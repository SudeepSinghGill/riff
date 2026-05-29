/**
 * rn-collection-view
 * Public API surface — grows with each milestone.
 */

// M0.3: native module (ping smoke test)
export { default as CollectionViewModule } from './specs/NativeCollectionViewModule';

// M0.2: types
export * from './types';

// M1.1: layout cache
export { LayoutCache, layoutCache } from './LayoutCache';

// M2.1: CollectionView component
export { Riff } from './components/CollectionView';
export type { RiffProps, RiffHandle } from './components/CollectionView';

// Custom layout plugin interface (legacy — use protocol types for new code)
export type { CustomLayoutPlugin, LayoutPluginContext } from './types/plugin';

// Layout protocol — unified interface for all layout engines
export type {
  RiffLayout,
  LayoutContext,
  SectionInfo,
  SupplementaryInfo,
  RiffSupplementaryAlignment,
  RiffPinBehavior,
  RiffStickyMode,
  RiffInvalidationScope,
  RiffListConfig,
  RiffMasonryConfig,
  RiffGridConfig,
  RiffFlowConfig,
  RiffCustomConfig,
  RiffSupplementary,
  RiffSection,
  RiffRenderItemInfo,
  RiffScrollOptions,
  RiffScrollOffsetOptions,
  JsLayoutScrollOptions,
  JsLayoutScrollResult,
} from './types/protocol';

// Layout factories (C++ built-ins)
export { list, masonry, grid, flow, customLayout } from './layouts';

// JS layout factories
export { radial } from './layouts/radial';
export { carousel3D } from './layouts/carousel3D';
export { spiral } from './layouts/spiral';
export { hex } from './layouts/hex';

// Sub-container component for compositional layouts
export { RiffSubContainer, CollectionSubContainer } from './components/CollectionSubContainer';
export type { RiffSubContainerProps, CollectionSubContainerProps } from './components/CollectionSubContainer';
