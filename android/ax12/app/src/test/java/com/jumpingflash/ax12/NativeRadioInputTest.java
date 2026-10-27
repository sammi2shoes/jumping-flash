package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28)
public class NativeRadioInputTest {
  static byte[] actualFrame() {
    String h =
        "A663100204010100FFFF0CFE01000CFEF4010080008000800080008000800CFEF4010080008000800080008000800CFEF401F4010CFE0000F4010080008000800080008000800080008000800CFE00000080008000800080008000000000010082014B";
    byte[] b = new byte[h.length() / 2];
    for (int i = 0; i < b.length; i++)
      b[i] = (byte) Integer.parseInt(h.substring(i * 2, i * 2 + 2), 16);
    return b;
  }

  @Test
  public void realCaptureIncludesKnobAndSwitches() {
    UmbusProbe d = new UmbusProbe();
    byte[] b = actualFrame();
    for (byte v : b) d.feed(new byte[] {v}, 1, 100);
    assertEquals(1, d.channelCandidates);
    assertTrue(d.exactLiveLayout);
    assertTrue(NativeRadioInput.valid(d.sources));
    assertEquals(500, d.sources[13]);
    assertEquals(500, d.sources[21]);
    assertEquals(500, d.sources[22]);
    assertEquals(-500, d.sources[23]);
    NativeRadioInput.Sample s = new NativeRadioInput.Sample(d.sources, 100, 1);
    assertTrue(s.fresh(219));
    assertFalse(s.fresh(220));
    assertFalse(s.fresh(99));
  }

  @Test
  public void invalidSwitchesAndUnavailableValuesBlockInputs() {
    UmbusProbe d = new UmbusProbe();
    byte[] b = actualFrame();
    d.feed(b, b.length, 100);
    for (int i : NativeRadioInput.SOURCES) {
      short[] v = d.sources.clone();
      v[i] = Short.MIN_VALUE;
      assertFalse(NativeRadioInput.valid(v));
    }
    d.sources[21] = 250;
    assertFalse(NativeRadioInput.valid(d.sources));
  }

  @Test
  public void continuousReaderNeedsOwnershipAndClosesOnStop() throws Exception {
    NativeUartProbeTest.FakePort p = new NativeUartProbeTest.FakePort();
    p.packet = actualFrame();
    NativeRadioInput r = new NativeRadioInput(p);
    assertFalse(r.start(false));
    assertEquals(0, p.opens);
    assertTrue(r.start(true));
    assertFalse(r.start(true));
    assertTrue(p.read.await(2, TimeUnit.SECONDS));
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    while (r.sample() == null && System.nanoTime() < end) Thread.yield();
    assertNotNull(r.sample());
    r.stop();
    assertNull(r.sample());
    while (r.running() && System.nanoTime() < end) Thread.yield();
    assertFalse(r.running());
    assertEquals(1, p.closes);
  }

  @Test
  public void invalidReportDoesNotRequireManualRestart() throws Exception {
    java.util.concurrent.ConcurrentLinkedQueue<byte[]> queue =
        new java.util.concurrent.ConcurrentLinkedQueue<>();
    NativeUartProbe.Port port =
        new NativeUartProbe.Port() {
          public boolean available() {
            return true;
          }

          public String availability() {
            return "test";
          }

          public String describe(String p) {
            return p;
          }

          public int open(String p) {
            return 1;
          }

          public String error(int e) {
            return "test";
          }

          public void close(int fd) {}

          public int read(int fd, byte[] out) {
            byte[] next = queue.poll();
            if (next == null) {
              try {
                Thread.sleep(2);
              } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
              }
              return 0;
            }
            System.arraycopy(next, 0, out, 0, next.length);
            return next.length;
          }
        };
    byte[] bad = actualFrame();
    bad[6 + 23 * 2] = 0;
    bad[7 + 23 * 2] = 0;
    bad[98] = (byte) UmbusProbe.crc8(bad, 1, 98);
    queue.add(bad);
    NativeRadioInput reader = new NativeRadioInput(port);
    reader.start(true);
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    try {
      while (!reader.status().contains("Waiting for valid") && System.nanoTime() < end)
        Thread.yield();
      assertTrue(reader.running());
      assertNull(reader.sample());
      assertTrue(reader.status().contains("Waiting for valid"));
      queue.add(actualFrame());
      while (reader.sample() == null && System.nanoTime() < end) Thread.yield();
      assertNotNull(reader.sample());
      assertTrue(reader.status().startsWith("LIVE"));
    } finally {
      reader.stop();
    }
  }

  @Test
  public void openPortAloneIsNotLiveAndOldReportsExpire() throws Exception {
    NativeUartProbeTest.FakePort io = new NativeUartProbeTest.FakePort();
    NativeRadioInput reader = new NativeRadioInput(io);
    reader.start(true);
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    try {
      assertTrue(io.entered.await(2, TimeUnit.SECONDS));
      while (!reader.status().contains("NOT LIVE") && System.nanoTime() < end) Thread.yield();
      assertNull(reader.sample());
      assertTrue(reader.status().contains("NOT LIVE"));
      assertFalse(reader.status().startsWith("LIVE"));
    } finally {
      reader.stop();
    }
    end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    while (reader.running() && System.nanoTime() < end) Thread.yield();
    io = new NativeUartProbeTest.FakePort();
    io.packet = actualFrame();
    reader = new NativeRadioInput(io);
    reader.start(true);
    end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    try {
      while (reader.sample() == null && System.nanoTime() < end) Thread.yield();
      assertNotNull(reader.sample());
      assertTrue(reader.status().startsWith("LIVE"));
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofMillis(120));
      assertTrue(reader.status().startsWith("STALE"));
      assertFalse(reader.sample().fresh(android.os.SystemClock.elapsedRealtime()));
    } finally {
      reader.stop();
    }
  }

  static final class ProbePort implements NativeUartProbe.Port {
    java.util.concurrent.atomic.AtomicInteger probes =
        new java.util.concurrent.atomic.AtomicInteger();
    volatile boolean answer = false, delivered = false;
    volatile int closes = 0, result = 7;
    final java.util.concurrent.CountDownLatch opened = new java.util.concurrent.CountDownLatch(1);

    public boolean available() {
      return true;
    }

    public String availability() {
      return "test";
    }

    public String describe(String p) {
      return p;
    }

    public String error(int e) {
      return "errno" + e;
    }

    public int open(String p) {
      opened.countDown();
      return 42;
    }

    public void close(int fd) {
      closes++;
    }

    public boolean supportsLinkProbe() {
      return true;
    }

    public int linkProbe(int fd) {
      assertEquals(42, fd);
      probes.incrementAndGet();
      return result;
    }

    public int read(int fd, byte[] out) {
      if (answer && probes.get() > 0 && !delivered) {
        delivered = true;
        byte[] b = actualFrame();
        System.arraycopy(b, 0, out, 0, b.length);
        return b.length;
      }
      try {
        Thread.sleep(2);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
      return 0;
    }
  }

  static void awaitProbe(ProbePort p, int count) {
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    while (p.probes.get() < count && System.nanoTime() < end) Thread.yield();
    assertEquals(count, p.probes.get());
  }

  @Test
  public void startupProbesAreBoundedAndDoNotCreateLiveInputs() throws Exception {
    ProbePort p = new ProbePort();
    NativeRadioInput r = new NativeRadioInput(p);
    r.start(true);
    try {
      assertTrue(p.opened.await(2, TimeUnit.SECONDS));
      assertEquals(0, p.probes.get());
      for (int i = 1; i <= 3; i++) {
        org.robolectric.shadows.ShadowSystemClock.advanceBy(
            java.time.Duration.ofMillis(i == 1 ? 250 : 2000));
        awaitProbe(p, i);
        assertNull(r.sample());
        assertTrue(r.status().contains("NOT LIVE"));
      }
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofSeconds(10));
      Thread.sleep(20);
      assertEquals(3, p.probes.get());
      assertTrue(r.details().contains("link_probe_tx_bytes=21"));
    } finally {
      r.stop();
    }
  }

  @Test
  public void acceptsInputsArrivingAfterProbeThenDoesNotTransmitAgain() throws Exception {
    ProbePort p = new ProbePort();
    p.answer = true;
    NativeRadioInput r = new NativeRadioInput(p);
    r.start(true);
    try {
      assertTrue(p.opened.await(2, TimeUnit.SECONDS));
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofMillis(250));
      awaitProbe(p, 1);
      long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
      while (r.sample() == null && System.nanoTime() < end) Thread.yield();
      assertNotNull(r.sample());
      assertTrue(r.status().startsWith("LIVE"));
      org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofSeconds(5));
      Thread.sleep(20);
      assertEquals(1, p.probes.get());
      assertTrue(r.status().startsWith("STALE"));
      assertTrue(r.details().contains("first_rx_hex=A663"));
    } finally {
      r.stop();
    }
  }

  @Test
  public void stoppingBeforeFirstProbeSuppressesAllStartupTransmits() throws Exception {
    ProbePort p = new ProbePort();
    NativeRadioInput r = new NativeRadioInput(p);
    r.start(true);
    assertTrue(p.opened.await(2, TimeUnit.SECONDS));
    r.stop();
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    while (r.running() && System.nanoTime() < end) Thread.yield();
    org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofSeconds(5));
    assertEquals(0, p.probes.get());
    assertEquals(1, p.closes);
  }

  @Test
  public void startupWriteFailureClosesPortAndDoesNotCreateInputs() throws Exception {
    ProbePort p = new ProbePort();
    p.result = -13;
    NativeRadioInput r = new NativeRadioInput(p);
    r.start(true);
    assertTrue(p.opened.await(2, TimeUnit.SECONDS));
    org.robolectric.shadows.ShadowSystemClock.advanceBy(java.time.Duration.ofMillis(250));
    awaitProbe(p, 1);
    long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
    while (r.running() && System.nanoTime() < end) Thread.yield();
    assertFalse(r.running());
    assertNull(r.sample());
    assertEquals(1, p.closes);
    assertTrue(r.status().contains("write failed"));
    assertTrue(r.details().contains("link_probe_tx_bytes=0"));
  }
}
