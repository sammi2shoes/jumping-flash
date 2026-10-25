package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import java.util.*;
import java.util.concurrent.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28)
public class NativeUartProbeTest {
  static final class FakePort implements NativeUartProbe.Port {
    volatile int opens = 0, reads = 0, closes = 0;
    int error = 0;
    boolean available = true;
    byte[] packet;
    boolean delivered = false, repeat = false;
    final CountDownLatch entered = new CountDownLatch(1), read = new CountDownLatch(1);

    public boolean available() {
      return available;
    }

    public String availability() {
      return "test unavailable";
    }

    public String describe(String p) {
      return p + " mode=test";
    }

    public String error(int e) {
      return e == -13 ? "Permission denied" : e == -16 ? "Device busy" : "test error";
    }

    public int open(String p) {
      opens++;
      entered.countDown();
      return error == 0 ? 42 : error;
    }

    public int read(int fd, byte[] b) {
      assertEquals(42, fd);
      reads++;
      if (packet != null && (!delivered || repeat)) {
        delivered = true;
        System.arraycopy(packet, 0, b, 0, packet.length);
        read.countDown();
        return packet.length;
      }
      try {
        Thread.sleep(5);
      } catch (InterruptedException ignored) {
      }
      return 0;
    }

    public void close(int fd) {
      assertEquals(42, fd);
      closes++;
    }
  }

  @Test
  public void ownershipConfirmationRequiredBeforeAnyAccess() {
    FakePort io = new FakePort();
    NativeUartProbe p = new NativeUartProbe(io, () -> {});
    assertFalse(p.start("/dev/ttyS0", false));
    assertFalse(p.running());
    assertEquals(0, io.opens);
  }

  @Test
  public void permissionDeniedAndBusyAreReportedWithoutReads() throws Exception {
    for (int code : new int[] {-13, -16}) {
      FakePort io = new FakePort();
      io.error = code;
      NativeUartProbe p = new NativeUartProbe(io, () -> {});
      assertTrue(p.start("/dev/ttyS0", true));
      assertTrue(p.awaitStopped(2000));
      assertEquals(0, io.reads);
      assertEquals(0, io.closes);
      assertTrue(p.dump().contains(code == -13 ? "Permission denied" : "Device busy"));
    }
  }

  @Test
  public void stopClosesExactlyOnceAndNeverStartsSecondReader() throws Exception {
    FakePort io = new FakePort();
    io.packet = UmbusProbeTest.candidate(0xa6, 321);
    NativeUartProbe p = new NativeUartProbe(io, () -> {});
    assertTrue(p.start("/dev/ttyS0", true));
    assertTrue(io.read.await(2, TimeUnit.SECONDS));
    assertFalse(p.start("/dev/ttyS4", true));
    p.stop();
    assertTrue(p.awaitStopped(2000));
    assertEquals(1, io.opens);
    assertEquals(1, io.closes);
    assertTrue(p.dump().contains("RAW SERIAL CAPTURE"));
    assertTrue(p.dump().contains("raw=321"));
  }

  @Test
  public void unavailableNativeLibraryDoesNotOpenPort() throws Exception {
    FakePort io = new FakePort();
    io.available = false;
    NativeUartProbe p = new NativeUartProbe(io, () -> {});
    p.start("/dev/ttyS0", true);
    assertTrue(p.awaitStopped(2000));
    assertEquals(0, io.opens);
    assertTrue(p.snapshot().contains("unavailable"));
  }

  @Test
  public void byteCapStopsAndClosesReader() throws Exception {
    FakePort io = new FakePort();
    io.packet = UmbusProbeTest.candidate(0xa6, 123);
    io.repeat = true;
    NativeUartProbe p = new NativeUartProbe(io, () -> {});
    p.start("/dev/ttyS0", true);
    assertTrue(p.awaitStopped(5000));
    assertEquals(1, io.closes);
    assertTrue(p.snapshot().contains("bytes=" + NativeUartProbe.MAX_BYTES));
    assertTrue(p.snapshot().contains("limit"));
  }

  @Test
  public void timeLimitStopsEvenWithNoBytes() throws Exception {
    FakePort io = new FakePort();
    NativeUartProbe p = new NativeUartProbe(io, () -> {});
    p.start("/dev/ttyS0", true);
    assertTrue(io.entered.await(2, TimeUnit.SECONDS));
    org.robolectric.shadows.ShadowSystemClock.advanceBy(
        java.time.Duration.ofMillis(NativeUartProbe.MAX_MS + 1));
    assertTrue(p.awaitStopped(2000));
    assertEquals(1, io.closes);
    assertTrue(p.snapshot().contains("limit"));
  }

  @Test(expected = IllegalArgumentException.class)
  public void arbitraryDevicePathsRejected() {
    new NativeUartProbe(new FakePort(), () -> {}).start("/dev/random", true);
  }
}
