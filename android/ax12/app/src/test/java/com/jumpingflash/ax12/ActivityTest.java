package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.*;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
public class ActivityTest {
  private Object get(MainActivity a, String n) throws Exception {
    Field f = MainActivity.class.getDeclaredField(n);
    f.setAccessible(true);
    return f.get(a);
  }

  private void set(MainActivity a, String n, Object v) throws Exception {
    Field f = MainActivity.class.getDeclaredField(n);
    f.setAccessible(true);
    f.set(a, v);
  }

  private Object invoke(MainActivity a, String n, Class<?> type, Object arg) throws Exception {
    Method m = MainActivity.class.getDeclaredMethod(n, type);
    m.setAccessible(true);
    return m.invoke(a, arg);
  }

  @Test
  public void dashboardAndAllTabsOpenWithoutBluetooth() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      for (int i = 0; i < 4; i++) {
        invoke(a, "showTab", int.class, i);
        View root = a.getWindow().getDecorView();
        root.measure(
            View.MeasureSpec.makeMeasureSpec(1280, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(720, View.MeasureSpec.EXACTLY));
        root.layout(0, 0, 1280, 720);
        Bitmap image = Bitmap.createBitmap(1280, 720, Bitmap.Config.ARGB_8888);
        root.draw(new Canvas(image));
        File dir = new File("build/screenshots");
        dir.mkdirs();
        try (FileOutputStream out = new FileOutputStream(new File(dir, "tab-" + i + ".png"))) {
          assertTrue(image.compress(Bitmap.CompressFormat.PNG, 100, out));
        }
      }
      assertFalse((Boolean) get(a, "armedRequest"));
      assertEquals(0, get(a, "txCount"));
    }
  }

  @Test
  public void pauseClearsArmAndPendingCycle() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      set(a, "armedRequest", true);
      set(a, "launchPending", true);
      c.pause();
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
      c.resume();
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }

  @Test
  public void focusLossDisarms() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      set(a, "armedRequest", true);
      a.onWindowFocusChanged(false);
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }

  @Test
  public void physicalEnableReleaseClearsArmAndCycle() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      set(a, "radioActive", false);
      set(a, "inputId", 42);
      set(a, "armedRequest", true);
      set(a, "launchPending", true);
      android.view.KeyEvent up =
          new android.view.KeyEvent(
              1,
              2,
              android.view.KeyEvent.ACTION_UP,
              android.view.KeyEvent.KEYCODE_BUTTON_R1,
              0,
              0,
              42,
              0,
              0,
              android.view.InputDevice.SOURCE_GAMEPAD);
      assertTrue(a.dispatchKeyEvent(up));
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
    }
  }

  @Test
  @SuppressWarnings("unchecked")
  public void axisSwitchSupportsBothDirectionsAndUnknownIsReleased() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      set(a, "enableAxis", android.view.MotionEvent.AXIS_Z);
      Method held = MainActivity.class.getDeclaredMethod("enableHeld");
      held.setAccessible(true);
      assertFalse((Boolean) held.invoke(a));
      java.util.Map<Integer, Float> axes = (java.util.Map<Integer, Float>) get(a, "axes");
      axes.put(android.view.MotionEvent.AXIS_Z, 1f);
      assertTrue((Boolean) held.invoke(a));
      set(a, "enableAxisLow", true);
      assertFalse((Boolean) held.invoke(a));
      axes.put(android.view.MotionEvent.AXIS_Z, -1f);
      assertTrue((Boolean) held.invoke(a));
    }
  }

  @Test
  @SuppressWarnings("unchecked")
  public void namedSbAndSdFlowThroughActivityWithoutQueuingDisarmedJump() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      android.hardware.input.InputManager im =
          (android.hardware.input.InputManager)
              a.getSystemService(android.content.Context.INPUT_SERVICE);
      android.util.SparseArray<android.view.InputDevice> devices = new android.util.SparseArray<>();
      devices.put(42, org.robolectric.shadows.ShadowInputDevice.makeInputDeviceNamed("AX12 test"));
      org.robolectric.util.ReflectionHelpers.setField(im, "mInputDevices", devices);
      set(a, "inputId", 42);
      Object attach =
          org.robolectric.util.ReflectionHelpers.getField(
              a.getWindow().getDecorView(), "mAttachInfo");
      org.robolectric.util.ReflectionHelpers.setField(attach, "mHasWindowFocus", true);
      RadioProfile p = (RadioProfile) get(a, "radio");
      for (int n = 0; n < 7; n++) p.binding[n].axis = n;
      p.binding[1].captured = p.binding[2].captured = 7;
      p.binding[0].captured = 5;
      java.util.Map<Integer, Float> values = (java.util.Map<Integer, Float>) get(a, "axes");
      for (int n = 0; n < 7; n++) values.put(n, 0f);
      values.put(0, -1f);
      values.put(4, -1f);
      values.put(5, -1f);
      values.put(6, -1f);
      ((BleLink) get(a, "ble")).ready = true;
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofSeconds(1));
      set(a, "telemetryAt", android.os.SystemClock.elapsedRealtime());
      set(a, "robotFlags", 3);
      set(a, "capabilities", 31);
      Method update = MainActivity.class.getDeclaredMethod("updateRadio");
      update.setAccessible(true);
      update.invoke(a);
      values.put(6, 1f);
      update.invoke(a);
      assertFalse((Boolean) get(a, "launchPending"));
      values.put(4, 0f);
      update.invoke(a);
      assertTrue((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
      set(a, "robotFlags", 7);
      values.put(6, -1f);
      update.invoke(a);
      assertTrue((Boolean) get(a, "launchPending"));
      values.put(4, -1f);
      update.invoke(a);
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
      values.put(0, 1f);
      values.put(4, 1f);
      update.invoke(a);
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }

  @Test
  public void telemetryRequiresSessionCrcAndNewSequence() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      byte[] b = new byte[20];
      b[0] = 74;
      b[1] = 70;
      b[2] = 2;
      b[3] = (byte) 0x81;
      Protocol.put(b, 4, 65535);
      Protocol.put(b, 16, (Integer) get(a, "session"));
      Protocol.put(b, 18, Protocol.crc(b, 18));
      invoke(a, "acceptTelemetry", byte[].class, b);
      assertEquals(1, get(a, "rxCount"));
      invoke(a, "acceptTelemetry", byte[].class, b);
      assertEquals(1, get(a, "rxCount"));
      Protocol.put(b, 4, 0);
      Protocol.put(b, 18, Protocol.crc(b, 18));
      invoke(a, "acceptTelemetry", byte[].class, b);
      assertEquals(2, get(a, "rxCount"));
      Protocol.put(b, 16, 0);
      Protocol.put(b, 18, Protocol.crc(b, 18));
      invoke(a, "acceptTelemetry", byte[].class, b);
      assertEquals(2, get(a, "rxCount"));
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }
}
