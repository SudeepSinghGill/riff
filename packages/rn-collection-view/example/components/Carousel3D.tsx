/**
 * Carousel3D — 3D perspective carousel with horizontal scroll.
 *
 * Items are arranged in a circle in 3D space. Horizontal scrolling rotates
 * the entire ring. Front-center items are full-size and facing the viewer;
 * items rotating away shrink, gain perspective rotateY, and fade slightly.
 *
 * Uses transform: [{ perspective }, { rotateY }, { scale }] driven by
 * scroll position — the kind of layout that is structurally impossible
 * in FlashList (linear sequential positions only).
 */
import React, { useCallback, useState } from 'react';
import { Dimensions, ScrollView, StyleSheet, View } from 'react-native';

interface Carousel3DProps<T> {
  data: T[];
  itemWidth?: number;
  itemHeight?: number;
  radius?: number;
  keyExtractor: (item: T, index: number) => string;
  renderItem: (info: { item: T; index: number }) => React.ReactElement;
}

export function Carousel3D<T>({
  data,
  itemWidth = 180,
  itemHeight = 220,
  radius: propRadius,
  keyExtractor,
  renderItem,
}: Carousel3DProps<T>) {
  const screenWidth = Dimensions.get('window').width;
  const screenHeight = Dimensions.get('window').height;
  const centerX = screenWidth / 2 - itemWidth / 2;
  const centerY = (screenHeight * 0.35) - itemHeight / 2;

  const n = data.length;
  if (n === 0) return null;

  const angleStep = (2 * Math.PI) / n;
  const radius = propRadius ?? Math.max(screenWidth * 0.9, 200);

  // Scroll drives rotation — one full revolution per scrollPerRevolution px.
  const scrollPerRevolution = screenWidth * 2;
  const [scrollX, setScrollX] = useState(0);

  const onScroll = useCallback((e: any) => {
    setScrollX(e.nativeEvent.contentOffset.x);
  }, []);

  const rotationOffset = -(scrollX / scrollPerRevolution) * 2 * Math.PI;

  // Content wide enough for several revolutions.
  const contentWidth = scrollPerRevolution * 5;

  // Sort items by depth (cos of angle) so back items render first.
  const items = data.map((item, i) => {
    const angle = rotationOffset + i * angleStep;
    // Normalize angle to [-PI, PI]
    const normAngle = ((angle % (2 * Math.PI)) + 3 * Math.PI) % (2 * Math.PI) - Math.PI;
    const cosA = Math.cos(normAngle);
    const sinA = Math.sin(normAngle);

    // x: sin gives left-right position on the circle
    const x = centerX + radius * sinA * 0.40; // spread items wider
    // Scale: front (cos=1) is full size, back (cos=-1) is small
    const scale = 0.4 + 0.6 * ((cosA + 1) / 2);
    // Perspective rotation
    const rotateYDeg = -normAngle * (180 / Math.PI) * 0.4; // subtle rotation
    // Opacity: fade items going behind
    const opacity = 0.3 + 0.7 * ((cosA + 1) / 2);
    // z-order: front items on top
    const zIndex = Math.round((cosA + 1) * 100);

    return { item, index: i, x, scale, rotateYDeg, opacity, zIndex, cosA };
  });

  // Sort by depth so back items render behind front items
  const sorted = [...items].sort((a, b) => a.cosA - b.cosA);

  return (
    <ScrollView
      horizontal
      style={S.flex}
      onScroll={onScroll}
      scrollEventThrottle={16}
      contentContainerStyle={{ width: contentWidth }}
      showsHorizontalScrollIndicator={false}
      decelerationRate="fast"
    >
      {/* Overlay the carousel items — offset by scrollX to stay in viewport */}
      <View style={[S.stage, { width: screenWidth }]} pointerEvents="box-none">
        {sorted.map(({ item, index, x, scale, rotateYDeg, opacity, zIndex }) => (
          <View
            key={keyExtractor(item, index)}
            style={{
              position: 'absolute',
              left: x + scrollX, // compensate for scroll
              top: centerY,
              width: itemWidth,
              height: itemHeight,
              zIndex,
              opacity,
              transform: [
                { perspective: 800 },
                { scale },
                { rotateY: `${rotateYDeg}deg` },
              ],
            }}
          >
            {renderItem({ item, index })}
          </View>
        ))}
      </View>
    </ScrollView>
  );
}

const S = StyleSheet.create({
  flex: { flex: 1 },
  stage: { height: '100%' },
});
