/**
 * PerfHood — floating performance overlay.
 *
 * Shows JS FPS, mount count, and render count.
 * Renders as a bottom-right absolute overlay with pointer events disabled.
 * Designed to be placed as the last child of a root View.
 */
import React from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { useFPS } from '../utils/useMetrics';

interface PerfHoodProps {
  mountCount: number;
  renderCount?: number;
}

export function PerfHood({ mountCount, renderCount }: PerfHoodProps) {
  const fps = useFPS();
  return (
    <View style={S.overlay} pointerEvents="none">
      <Text style={S.line}>FPS <Text style={S.val}>{fps}</Text></Text>
      <Text style={S.line}>Mounts <Text style={S.val}>{mountCount}</Text></Text>
      {renderCount != null && (
        <Text style={S.line}>Renders <Text style={S.val}>{renderCount}</Text></Text>
      )}
    </View>
  );
}

const S = StyleSheet.create({
  overlay: {
    position: 'absolute',
    bottom: 16,
    right: 12,
    backgroundColor: 'rgba(0,0,0,0.72)',
    borderRadius: 8,
    paddingVertical: 6,
    paddingHorizontal: 10,
    gap: 2,
    minWidth: 100,
  },
  line: { fontSize: 11, color: '#888', fontFamily: 'Menlo' },
  val:  { fontWeight: '700', color: '#4ade80' },
});
