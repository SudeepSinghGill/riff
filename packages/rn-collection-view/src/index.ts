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
// NOTE: During POC development the component lives in example/components/
// (avoids dual-React-instance crash from the package's own node_modules/react).
// Will re-export from here once the monorepo uses workspace-hoisted React.

// Custom layout plugin interface (legacy — use protocol types for new code)
export type { CustomLayoutPlugin, LayoutPluginContext } from './types/plugin';

// Layout protocol — unified interface for all layout engines
export type {
  CollectionViewLayout,
  LayoutContext,
  SectionInfo,
  SupplementaryInfo,
  SupplementaryAlignment,
  PinBehavior,
  StickyMode,
  InvalidationScope,
  ListLayoutDelegate,
  MasonryLayoutDelegate,
  GridLayoutDelegate,
  FlowLayoutDelegate,
  CustomLayoutDelegate,
  SupplementaryItem,
  SectionConfig,
  CollectionViewProps,
} from './types/protocol';

// Layout factories
export { list, masonry, grid, flow, customLayout } from './layouts';
