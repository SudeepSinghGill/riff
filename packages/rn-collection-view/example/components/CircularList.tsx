/**
 * CircularList — Items arranged in a radial arc via TS layout computation.
 * Scroll vertically to rotate items around the arc.
 *
 * Demonstrates arbitrary 2D positioning that is structurally impossible in
 * FlashList (which assumes sequential linear positions along one axis).
 *
 * Used in the FlashList comparison demo (Tab 4).
 */
import React, { useCallback, useRef, useState } from 'react';
import { ScrollView, View, StyleSheet, Dimensions } from 'react-native';

interface CircularListProps<T> {
  data: T[];
  itemSize?: number;
  radius?: number;
  keyExtractor: (item: T, index: number) => string;
  renderItem: (info: { item: T; index: number }) => React.ReactElement;
}

export function CircularList<T>({
  data,
  itemSize = 80,
  radius: propRadius,
  keyExtractor,
  renderItem,
}: CircularListProps<T>) {
  const screenWidth = Dimensions.get('window').width;
  const radius = propRadius ?? Math.min(screenWidth * 0.35, 150);
  const cx = screenWidth / 2 - itemSize / 2;
  const cy = radius + itemSize + 40; // center Y of the circle

  const n = data.length;
  const angleStep = n > 0 ? (2 * Math.PI) / n : 0;

  // Scroll drives rotation — one full revolution per `scrollPerRevolution` px.
  const scrollPerRevolution = 600;
  const [scrollY, setScrollY] = useState(0);

  const onScroll = useCallback((e: any) => {
    setScrollY(e.nativeEvent.contentOffset.y);
  }, []);

  const rotationOffset = (scrollY / scrollPerRevolution) * 2 * Math.PI;

  // Content height: enough to allow several revolutions of scrolling.
  const contentHeight = scrollPerRevolution * 5;

  return (
    <ScrollView
      style={S.flex}
      onScroll={onScroll}
      scrollEventThrottle={16}
      contentContainerStyle={{ height: contentHeight }}
      showsVerticalScrollIndicator={false}
    >
      {data.map((item, i) => {
        const angle = rotationOffset + i * angleStep;
        const x = cx + radius * Math.cos(angle);
        const y = cy + radius * Math.sin(angle) + scrollY; // compensate for scroll
        // Scale: items at the "front" (bottom of circle, sin=1) are larger.
        const scale = 0.6 + 0.4 * ((Math.sin(angle) + 1) / 2);
        // zIndex: front items render on top.
        const z = Math.round((Math.sin(angle) + 1) * 50);

        return (
          <View
            key={keyExtractor(item, i)}
            style={{
              position: 'absolute',
              left: x,
              top: y,
              width: itemSize,
              height: itemSize,
              zIndex: z,
              transform: [{ scale }],
              opacity: 0.4 + 0.6 * ((Math.sin(angle) + 1) / 2),
            }}
          >
            {renderItem({ item, index: i })}
          </View>
        );
      })}
    </ScrollView>
  );
}

const S = StyleSheet.create({ flex: { flex: 1 } });
