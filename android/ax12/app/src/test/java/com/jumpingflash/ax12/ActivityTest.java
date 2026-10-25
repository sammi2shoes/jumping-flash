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
      for (int i = 0; i < 5; i++) {
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
  public void rotaryScrollFromAnotherDeviceIsCapturedWithoutBecomingRobotInput() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      android.view.MotionEvent.PointerProperties pp =
          new android.view.MotionEvent.PointerProperties();
      pp.id = 0;
      android.view.MotionEvent.PointerCoords pc = new android.view.MotionEvent.PointerCoords();
      pc.setAxisValue(android.view.MotionEvent.AXIS_SCROLL, 3f);
      android.view.MotionEvent event =
          android.view.MotionEvent.obtain(
              1,
              2,
              android.view.MotionEvent.ACTION_SCROLL,
              1,
              new android.view.MotionEvent.PointerProperties[] {pp},
              new android.view.MotionEvent.PointerCoords[] {pc},
              0,
              0,
              1,
              1,
              77,
              0,
              android.view.InputDevice.SOURCE_ROTARY_ENCODER,
              0);
      a.dispatchGenericMotionEvent(event);
      event.recycle();
      String trace = ((InputTrace) get(a, "inputTrace")).dump();
      assertTrue(trace.contains("device=77"));
      assertTrue(trace.contains("ACTION_SCROLL"));
      assertTrue(trace.contains("AXIS_SCROLL=3.0"));
      assertEquals(0L, get(a, "inputAt"));
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }

  @Test
  public void nonGamepadKeysAndInventoryAreDiagnosticOnly() throws Exception {
    InputTrace trace = new InputTrace();
    android.view.KeyEvent key =
        new android.view.KeyEvent(
            1,
            2,
            android.view.KeyEvent.ACTION_DOWN,
            android.view.KeyEvent.KEYCODE_VOLUME_UP,
            0,
            0,
            88,
            0,
            0,
            android.view.InputDevice.SOURCE_KEYBOARD);
    trace.key(key);
    assertTrue(trace.dump().contains("device=88"));
    assertTrue(trace.dump().contains("code=24"));
    assertTrue(InputTrace.inventory().contains("INPUT DEVICE INVENTORY"));
    trace.clear();
    assertEquals(0, trace.keyEvents);
    assertFalse(trace.dump().contains("device=88"));
  }

  @Test
  public void diagnosticExportContainsAllSourceTraceAndDeviceInventory() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      InputTrace trace = (InputTrace) get(a, "inputTrace");
      trace.key(
          new android.view.KeyEvent(
              android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_VOLUME_UP));
      java.io.File dir = new java.io.File("build/export-tests");
      dir.mkdirs();
      java.io.File file = new java.io.File(dir, "diagnostic.txt");
      a.onActivityResult(
          30,
          android.app.Activity.RESULT_OK,
          new android.content.Intent().setData(android.net.Uri.fromFile(file)));
      String exported =
          new String(
              java.nio.file.Files.readAllBytes(file.toPath()),
              java.nio.charset.StandardCharsets.UTF_8);
      assertTrue(exported.contains("ANDROID INPUT DEVICE INVENTORY"));
      assertTrue(exported.contains("ALL-SOURCE INPUT TRACE"));
      assertTrue(exported.contains("code=24"));
      assertTrue(exported.contains("Named profile"));
    }
  }

  @Test
  public void nativeProbeModeBlocksAllArmAndLaunchEntrypoints() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      set(a, "armedRequest", true);
      set(a, "launchPending", true);
      Method enter = MainActivity.class.getDeclaredMethod("enterNativeProbeMode");
      enter.setAccessible(true);
      enter.invoke(a);
      assertTrue((Boolean) get(a, "nativeProbeMode"));
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
      for (String name :
          new String[] {
            "requestArm", "requestRadioArm", "requestLaunch", "requestRadioJump", "sendFrame"
          }) {
        Method m = MainActivity.class.getDeclaredMethod(name);
        m.setAccessible(true);
        m.invoke(a);
      }
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
      assertEquals(0, get(a, "txCount"));
    }
  }

  @Test
  public void backgroundingStopsNativeReaderAndKeepsProbeModeLatched() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      NativeUartProbeTest.FakePort io = new NativeUartProbeTest.FakePort();
      NativeUartProbe probe = new NativeUartProbe(io, () -> {});
      set(a, "nativeProbe", probe);
      set(a, "nativeProbeMode", true);
      assertTrue(probe.start("/dev/ttyS0", true));
      assertTrue(io.entered.await(2, java.util.concurrent.TimeUnit.SECONDS));
      c.pause();
      assertTrue(probe.awaitStopped(2000));
      assertEquals(1, io.closes);
      assertTrue((Boolean) get(a, "nativeProbeMode"));
      assertFalse((Boolean) get(a, "armedRequest"));
    }
  }

  @Test
  public void operatorStopAlsoStopsReadOnlyCapture() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      NativeUartProbeTest.FakePort io = new NativeUartProbeTest.FakePort();
      NativeUartProbe probe = new NativeUartProbe(io, () -> {});
      set(a, "nativeProbe", probe);
      set(a, "nativeProbeMode", true);
      set(a, "armedRequest", true);
      set(a, "launchPending", true);
      probe.start("/dev/ttyS0", true);
      assertTrue(io.entered.await(2, java.util.concurrent.TimeUnit.SECONDS));
      Method stop = MainActivity.class.getDeclaredMethod("operatorStop");
      stop.setAccessible(true);
      stop.invoke(a);
      assertTrue(probe.awaitStopped(2000));
      assertEquals(1, io.closes);
      assertFalse((Boolean) get(a, "armedRequest"));
      assertFalse((Boolean) get(a, "launchPending"));
    }
  }

  @Test
  public void probeModeBlockSurvivesActivityRecreation() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      Method enter = MainActivity.class.getDeclaredMethod("enterNativeProbeMode");
      enter.setAccessible(true);
      enter.invoke(a);
      assertTrue(a.getSharedPreferences("ax12", 0).getBoolean("nativeProbeMode", false));
      c.recreate();
      assertTrue((Boolean) get(c.get(), "nativeProbeMode"));
      assertFalse((Boolean) get(c.get(), "armedRequest"));
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

  @Test
  public void nativeSingleLegArmingNeedsFreshReportsOffEdgeAndNoAndroidDevice() throws Exception {
    try (ActivityController<MainActivity> c =
        Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity a = c.get();
      Object attach =
          org.robolectric.util.ReflectionHelpers.getField(
              a.getWindow().getDecorView(), "mAttachInfo");
      org.robolectric.util.ReflectionHelpers.setField(attach, "mHasWindowFocus", true);
      Method select = MainActivity.class.getDeclaredMethod("selectNativeInputs");
      select.setAccessible(true);
      select.invoke(a);
      NativeRadioInput reader = (NativeRadioInput) get(a, "nativeRadio");
      org.robolectric.util.ReflectionHelpers.setField(reader, "running", true);
      ((BleLink) get(a, "ble")).ready = true;
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofSeconds(1));
      long now = android.os.SystemClock.elapsedRealtime();
      set(a, "telemetryAt", now);
      set(a, "robotFlags", 3);
      set(a, "capabilities", 32);
      UmbusProbe d = new UmbusProbe();
      byte[] frame = NativeRadioInputTest.actualFrame();
      d.feed(frame, frame.length, now);
      short[] values = d.sources.clone();
      values[1] = values[3] = 0;
      values[2] = -500;
      values[22] = 0;
      Method consume = MainActivity.class.getDeclaredMethod("consumeNativeInputs");
      consume.setAccessible(true);
      // Opening with SB already armed cannot arm, despite valid telemetry.
      org.robolectric.util.ReflectionHelpers.setField(
          reader, "latest", new NativeRadioInput.Sample(values, now, 1));
      consume.invoke(a);
      assertFalse((Boolean) get(a, "armedRequest"));
      values[21] = -500;
      org.robolectric.util.ReflectionHelpers.setField(
          reader, "latest", new NativeRadioInput.Sample(values, now, 2));
      consume.invoke(a);
      values[21] = 0;
      org.robolectric.util.ReflectionHelpers.setField(
          reader, "latest", new NativeRadioInput.Sample(values, now, 3));
      consume.invoke(a);
      assertTrue((Boolean) get(a, "armedRequest"));
      assertEquals(-1, get(a, "inputId"));
      // SD does not request jump on manual-only firmware.
      set(a, "robotFlags", 7);
      values[23] = 500;
      org.robolectric.util.ReflectionHelpers.setField(
          reader, "latest", new NativeRadioInput.Sample(values, now, 4));
      consume.invoke(a);
      assertFalse((Boolean) get(a, "launchPending"));
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofMillis(120));
      consume.invoke(a);
      assertFalse((Boolean) get(a, "armedRequest"));
      now = android.os.SystemClock.elapsedRealtime();
      set(a, "telemetryAt", now);
      org.robolectric.util.ReflectionHelpers.setField(
          reader, "latest", new NativeRadioInput.Sample(values, now, 5));
      consume.invoke(a);
      assertFalse(
          (Boolean) get(a, "armedRequest")); // New heartbeat with SB still armed does not rearm.
      org.robolectric.util.ReflectionHelpers.setField(reader, "running", false);
    }
  }
}
