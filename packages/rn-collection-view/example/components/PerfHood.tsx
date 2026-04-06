/**
 * PerfHood — rich live performance overlay for Riff vs FlashList comparison.
 *
 * Displays 6 metric rows from native + JS measurement:
 *   UI  — CADisplayLink native FPS + frame time
 *   JS  — rAF FPS + idle headroom %
 *   CPU — main-thread utilization % (Mach thread_info)
 *   Cells — active / total mount count
 *   Mem — memory delta from session start (os_proc_available_memory)
 *   Vel — scroll velocity in pt/s (when scrolling)
 *
 * Color coding per row: green/yellow/red thresholds.
 * Pointer events disabled (overlay only — no touch interception).
 * A single 500ms polling interval drives all native reads.
 */
import React from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { usePerformanceMetrics } from '../utils/useMetrics';

interface PerfHoodProps {
  /** Currently mounted cell count (decremented on unmount). */
  activeMounts: number;
  /** Total cells ever mounted (cumulative, never decremented). */
  totalMounts: number;
  /** Scroll velocity in pt/s (pass from onScroll handler). */
  scrollVelocity?: number;
}

// ── Color helpers ─────────────────────────────────────────────────────────────

function fpsColor(fps: number): string {
  if (fps >= 55) return '#4ade80'; // green
  if (fps >= 40) return '#facc15'; // yellow
  return '#f87171';                // red
}

function idleColor(pct: number): string {
  if (pct >= 70) return '#4ade80';
  if (pct >= 40) return '#facc15';
  return '#f87171';
}

function cpuColor(pct: number): string {
  if (pct < 0)   return '#555';    // unavailable
  if (pct < 50)  return '#4ade80';
  if (pct < 80)  return '#facc15';
  return '#f87171';
}

function memColor(mb: number): string {
  if (mb < 20) return '#4ade80';
  if (mb < 50) return '#facc15';
  return '#f87171';
}

// ── Component ─────────────────────────────────────────────────────────────────

export function PerfHood({ activeMounts, totalMounts, scrollVelocity = 0 }: PerfHoodProps) {
  const m = usePerformanceMetrics();

  const velStr = scrollVelocity > 5
    ? scrollVelocity >= 1000
      ? `${(scrollVelocity / 1000).toFixed(1)}k`
      : `${Math.round(scrollVelocity)}`
    : null;

  return (
    <View style={S.overlay} pointerEvents="none">

      {/* UI (native CADisplayLink) FPS */}
      <View style={S.row}>
        <Text style={S.label}>UI </Text>
        <Text style={[S.val, { color: fpsColor(m.nativeFPS) }]}>{m.nativeFPS}</Text>
        <Text style={S.unit}>fps </Text>
        <Text style={S.dim}>{m.frameTimeMs.toFixed(1)}ms</Text>
      </View>

      {/* JS thread FPS + idle */}
      <View style={S.row}>
        <Text style={S.label}>JS </Text>
        <Text style={[S.val, { color: fpsColor(m.jsFPS) }]}>{m.jsFPS}</Text>
        <Text style={S.unit}>fps </Text>
        <Text style={S.dim}>idle </Text>
        <Text style={[S.val, { color: idleColor(m.jsIdlePct) }]}>{m.jsIdlePct}</Text>
        <Text style={S.unit}>%</Text>
      </View>

      {/* Main thread CPU */}
      <View style={S.row}>
        <Text style={S.label}>CPU </Text>
        {m.mainThreadCPU >= 0 ? (
          <>
            <Text style={[S.val, { color: cpuColor(m.mainThreadCPU) }]}>{m.mainThreadCPU}</Text>
            <Text style={S.unit}>%</Text>
          </>
        ) : (
          <Text style={S.dim}>—</Text>
        )}
      </View>

      {/* Active / total mounts */}
      <View style={S.row}>
        <Text style={S.label}>Cells </Text>
        <Text style={S.val}>{activeMounts}</Text>
        <Text style={S.dim}>/{totalMounts}</Text>
      </View>

      {/* Memory delta */}
      <View style={S.row}>
        <Text style={S.label}>Mem </Text>
        <Text style={[S.val, { color: memColor(Math.abs(m.memoryDeltaMB)) }]}>
          {m.memoryDeltaMB >= 0 ? '+' : ''}{m.memoryDeltaMB.toFixed(1)}
        </Text>
        <Text style={S.unit}> MB</Text>
        {m.pressureLevel > 0 && (
          <Text style={[S.unit, { color: m.pressureLevel === 2 ? '#f87171' : '#facc15' }]}>
            {' '}{m.pressureLevel === 2 ? '⚠' : '△'}
          </Text>
        )}
      </View>

      {/* Scroll velocity — only shown when scrolling */}
      {velStr != null && (
        <View style={S.row}>
          <Text style={S.label}>Vel </Text>
          <Text style={S.val}>{velStr}</Text>
          <Text style={S.unit}> pt/s</Text>
        </View>
      )}
    </View>
  );
}

// ── Styles ────────────────────────────────────────────────────────────────────

const S = StyleSheet.create({
  overlay: {
    position: 'absolute',
    bottom: 16,
    right: 10,
    backgroundColor: 'rgba(0,0,0,0.80)',
    borderRadius: 8,
    paddingVertical: 7,
    paddingHorizontal: 10,
    gap: 3,
    minWidth: 140,
  },
  row:   { flexDirection: 'row', alignItems: 'baseline' },
  label: { fontSize: 10, color: '#555', fontFamily: 'Menlo', width: 34 },
  val:   { fontSize: 11, fontWeight: '700', color: '#ddd', fontFamily: 'Menlo' },
  unit:  { fontSize: 10, color: '#555', fontFamily: 'Menlo' },
  dim:   { fontSize: 10, color: '#444', fontFamily: 'Menlo' },
});
