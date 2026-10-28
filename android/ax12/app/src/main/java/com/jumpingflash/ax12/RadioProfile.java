package com.jumpingflash.ax12;

import java.util.*;

/** Named RC controls. RC channel numbers are labels, never assumed Android axis IDs. */
public final class RadioProfile {
  public static final int THROTTLE = 0,
      AILERON = 1,
      ELEVATOR = 2,
      HEIGHT = 3,
      SB = 4,
      SC = 5,
      SD = 6;
  public static final String[] LABELS = {
    "CH3 • Throttle / spinner",
    "CH1 • Aileron / steering",
    "CH2 • Elevator / forward",
    "S2 • Standing height",
    "SB • Disarmed / drive / full",
    "SC • Hop / gyro walk / ambulate",
    "SD • Full-power jump on either edge"
  };
  public static final String[] ARM_NAMES = {"DISARMED", "DRIVE ONLY", "FULLY ARMED"},
      STYLE_NAMES = {"HOP", "GYRO WALK", "AMBULATE"};

  public static final class Binding {
    public int axis = -1, lowKey = -1, highKey = -1;
    public float low = -1, center = 0, high = 1;
    public boolean inverted = false;
    public int captured = 0;

    public float sample(Map<Integer, Float> axes, Set<Integer> keys, Set<Integer> seenKeys) {
      float t;
      if (axis >= 0) {
        Float v = axes.get(axis);
        if (v == null
            || !Float.isFinite(v)
            || !Float.isFinite(low)
            || !Float.isFinite(high)
            || Math.abs(high - low) < .0001f) return Float.NaN;
        t = (v - low) / (high - low);
      } else if (highKey >= 0) {
        if (lowKey >= 0) {
          boolean lo = keys.contains(lowKey), hi = keys.contains(highKey);
          if (lo && hi) return Float.NaN;
          if (lo) t = 0;
          else if (hi) t = 1;
          else if (seenKeys.contains(lowKey) || seenKeys.contains(highKey)) t = .5f;
          else return Float.NaN;
        } else {
          if (!seenKeys.contains(highKey)) return Float.NaN;
          t = keys.contains(highKey) ? 1 : 0;
        }
      } else return Float.NaN;
      t = Math.max(0, Math.min(1, t));
      return inverted ? 1 - t : t;
    }

    public float bipolar(Map<Integer, Float> axes, Set<Integer> keys, Set<Integer> seen, float dz) {
      float t = sample(axes, keys, seen);
      if (!Float.isFinite(t)) return Float.NaN;
      float c = (center - low) / (high - low);
      if (inverted) c = 1 - c;
      if (!(c > .01f && c < .99f)) return Float.NaN;
      float v = t >= c ? (t - c) / (1 - c) : (t - c) / c;
      return Math.abs(v) <= dz ? 0 : Math.copySign((Math.abs(v) - dz) / (1 - dz), v);
    }
  }

  public final Binding[] binding = new Binding[7];

  public RadioProfile() {
    for (int i = 0; i < 7; i++) binding[i] = new Binding();
  }

  public static int threePosition(float v, int previous) {
    if (!Float.isFinite(v)) return -1;
    if (previous == 0 && v < .38f) return 0;
    if (previous == 1 && v > .28f && v < .72f) return 1;
    if (previous == 2 && v > .62f) return 2;
    return v < .33f ? 0 : v > .67f ? 2 : 1;
  }

  public static final String[] LEG_MIX_NAMES = {"Side assist", "Differential", "Select a leg"};

  /** Left/right normalized position rates for the dual-leg bench, not positions. */
  public static float[] legBenchMix(float elevator, float aileron) {
    return legBenchMix(elevator, aileron, 0);
  }

  public static float[] legBenchMix(float elevator, float aileron, int mix) {
    float e = Math.abs(elevator) <= .08f ? 0 : elevator,
        a = Math.abs(aileron) <= .08f ? 0 : aileron;
    float l, r;
    if (mix == 1) {
      l = e - a;
      r = e + a;
    } else if (mix == 2) {
      l = a > .25f ? 0 : e;
      r = a < -.25f ? 0 : e;
    } else {
      l = e + Math.max(0, -a);
      r = e + Math.max(0, a);
    }
    return new float[] {Math.max(-1, Math.min(1, l)), Math.max(-1, Math.min(1, r))};
  }

  public static float[] mixPreview(float forward, float turn) {
    float l = forward + turn,
        r = forward - turn,
        scale = Math.max(1, Math.max(Math.abs(l), Math.abs(r)));
    return new float[] {l / scale, r / scale};
  }

  public static final class Snapshot {
    public float throttle = Float.NaN, aileron = Float.NaN, elevator = Float.NaN, height = .5f;
    public int sb = -1, sc = -1, sd = -1;

    public boolean driveKnown() {
      return Float.isFinite(aileron) && Float.isFinite(elevator) && sb >= 0 && sc >= 0;
    }
  }

  public Snapshot sample(
      Map<Integer, Float> axes, Set<Integer> keys, Set<Integer> seen, Snapshot previous, float dz) {
    Snapshot s = new Snapshot();
    s.throttle = binding[THROTTLE].sample(axes, keys, seen);
    s.aileron = binding[AILERON].bipolar(axes, keys, seen, dz);
    s.elevator = binding[ELEVATOR].bipolar(axes, keys, seen, dz);
    if (binding[HEIGHT].axis >= 0) s.height = binding[HEIGHT].sample(axes, keys, seen);
    s.sb = threePosition(binding[SB].sample(axes, keys, seen), previous.sb);
    s.sc = threePosition(binding[SC].sample(axes, keys, seen), previous.sc);
    float d = binding[SD].sample(axes, keys, seen);
    s.sd = Float.isFinite(d) ? (d > .5f ? 1 : 0) : -1;
    return s;
  }

  public boolean driveCalibrated() {
    return (binding[AILERON].captured & 7) == 7 && (binding[ELEVATOR].captured & 7) == 7;
  }

  public boolean distinct() {
    Set<Integer> a = new HashSet<>(), k = new HashSet<>();
    for (Binding b : binding) {
      if (b.axis >= 0 && !a.add(b.axis)) return false;
      for (int key : new int[] {b.lowKey, b.highKey}) if (key >= 0 && !k.add(key)) return false;
    }
    return true;
  }
}
