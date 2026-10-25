package com.jumpingflash.ax12;

import android.os.SystemClock;
import android.view.*;
import java.util.*;

/** Observational only: never updates control bindings, arming or input freshness. */
public final class InputTrace {
  private final ArrayDeque<String> events = new ArrayDeque<>();
  private final Map<String, Long> lastMotion = new HashMap<>();
  public int motionEvents = 0, keyEvents = 0;

  private void add(String s) {
    events.addLast(SystemClock.elapsedRealtime() + " " + s);
    while (events.size() > 2000) events.removeFirst();
  }

  public void clear() {
    events.clear();
    lastMotion.clear();
    motionEvents = keyEvents = 0;
  }

  public void motion(MotionEvent e) {
    motionEvents++;
    String route = e.getDeviceId() + ":" + e.getSource() + ":" + e.getActionMasked();
    long now = SystemClock.elapsedRealtime();
    // Rate-limit repetitive moves/hover, but retain every relative scroll event.
    if (e.getActionMasked() != MotionEvent.ACTION_SCROLL
        && now - lastMotion.getOrDefault(route, -1000L) < 50) return;
    lastMotion.put(route, now);
    StringBuilder s =
        new StringBuilder("MOTION device=")
            .append(e.getDeviceId())
            .append(" source=0x")
            .append(Integer.toHexString(e.getSource()))
            .append(" action=")
            .append(MotionEvent.actionToString(e.getAction()))
            .append(" ");
    InputDevice d = e.getDevice();
    Set<Integer> declared = new HashSet<>();
    if (d != null) {
      s.append("name=").append(d.getName()).append(' ');
      for (InputDevice.MotionRange r : d.getMotionRanges()) declared.add(r.getAxis());
    }
    for (int axis = 0; axis <= 63; axis++) {
      float v = e.getAxisValue(axis);
      if (v != 0 || declared.contains(axis))
        s.append(MotionEvent.axisToString(axis)).append('=').append(v).append(' ');
    }
    add(s.toString());
  }

  public void key(KeyEvent e) {
    keyEvents++;
    add(
        "KEY device="
            + e.getDeviceId()
            + " source=0x"
            + Integer.toHexString(e.getSource())
            + " code="
            + e.getKeyCode()
            + " "
            + KeyEvent.keyCodeToString(e.getKeyCode())
            + " action="
            + e.getAction()
            + " repeat="
            + e.getRepeatCount());
  }

  public String recent() {
    StringBuilder s =
        new StringBuilder("All-source diagnostic events: motion=")
            .append(motionEvents)
            .append(" key=")
            .append(keyEvents)
            .append('\n');
    int skip = Math.max(0, events.size() - 10), i = 0;
    for (String e : events) if (i++ >= skip) s.append(e).append('\n');
    return s.toString();
  }

  public String dump() {
    StringBuilder s =
        new StringBuilder("ALL-SOURCE INPUT TRACE (independent of selected controller)\n");
    for (String e : events) s.append(e).append('\n');
    return s.toString();
  }

  public static String inventory() {
    StringBuilder s = new StringBuilder("ANDROID INPUT DEVICE INVENTORY\n");
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (d == null) continue;
      s.append("id=")
          .append(id)
          .append(" name=")
          .append(d.getName())
          .append(" descriptor=")
          .append(d.getDescriptor())
          .append(" sources=0x")
          .append(Integer.toHexString(d.getSources()))
          .append(" vendor=")
          .append(d.getVendorId())
          .append(" product=")
          .append(d.getProductId())
          .append('\n');
      for (InputDevice.MotionRange r : d.getMotionRanges())
        s.append("  ")
            .append(MotionEvent.axisToString(r.getAxis()))
            .append(" source=0x")
            .append(Integer.toHexString(r.getSource()))
            .append(" min=")
            .append(r.getMin())
            .append(" max=")
            .append(r.getMax())
            .append(" flat=")
            .append(r.getFlat())
            .append(" resolution=")
            .append(r.getResolution())
            .append('\n');
    }
    return s.toString();
  }
}
