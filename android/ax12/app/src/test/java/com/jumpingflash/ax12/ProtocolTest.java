package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import org.junit.Test;

public class ProtocolTest {
  @Test
  public void crcGoldenVector() {
    assertEquals(0x29b1, Protocol.crc("123456789".getBytes(), 9));
  }

  @Test
  public void disarmClearsMotionAndCycle() {
    byte[] b = Protocol.command(65535, 0x1234, false, 1, 1, -1, true);
    assertTrue(Protocol.valid(b, 1));
    assertEquals(0, b[6]);
    assertEquals(0, Protocol.s16(b, 8));
    assertEquals(0, Protocol.s16(b, 10));
    assertEquals(0x1234, Protocol.u16(b, 16));
    assertEquals(65535, Protocol.u16(b, 4));
  }

  @Test
  public void armedClampsAndEncodesSignedAxes() {
    byte[] b = Protocol.command(3, 7, true, 2, 3, -2, true);
    assertEquals(3, b[6]);
    assertEquals(1000, Protocol.s16(b, 8));
    assertEquals(-1000, Protocol.s16(b, 10));
    assertTrue(Protocol.valid(b, 1));
  }

  @Test
  public void corruptAndWrongFramesRejected() {
    byte[] b = Protocol.command(3, 7, true, 0, 0, 0, false);
    assertFalse(Protocol.valid(null, 1));
    assertFalse(Protocol.valid(new byte[19], 1));
    assertFalse(Protocol.valid(b, 0x81));
    for (int i = 0; i < b.length; i++) {
      byte[] changed = b.clone();
      changed[i] ^= 1;
      assertFalse(Protocol.valid(changed, 1));
    }
  }

  @Test
  public void invalidInputCannotEnable() {
    for (float f : new float[] {Float.NaN, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY}) {
      byte[] b = Protocol.command(0, 1, true, 0, f, .5f, true);
      assertEquals(0, b[6]);
      assertEquals(0, Protocol.s16(b, 8));
      assertEquals(0, Protocol.s16(b, 10));
    }
    assertEquals(0, Protocol.command(0, 1, true, 9, 1, 1, true)[6]);
  }
}
