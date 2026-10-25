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
}
