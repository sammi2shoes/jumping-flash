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

  @Test
  public void watchdogRequestIsAnExplicitDisarmedZeroSpeedPacket() {
    byte[] b = Protocol.watchdogCheck(91, 123, 2);
    assertTrue(Protocol.valid(b, 1));
    assertEquals(91, Protocol.u16(b, 4));
    assertEquals(123, Protocol.u16(b, 16));
    assertEquals(0, b[6]);
    assertEquals(0, b[14]);
    assertEquals(1, b[15]);
    for (int i = 8; i <= 13; i++) assertEquals(0, b[i]);
  }

  @Test
  public void directionStopCancelsMaintenanceAndNeverRequestsArm() {
    byte[] b = Protocol.directionStop(9, 12, 2, 1);
    assertTrue(Protocol.valid(b, 1));
    assertEquals(0, b[6]);
    assertEquals(1, b[14]);
    assertEquals(2, b[15]);
    for (int i = 8; i <= 13; i++) assertEquals(0, b[i]);
  }

  @Test
  public void referenceCaptureIsDisarmedNeutralAndSessionBound() {
    byte[] b = Protocol.referenceCapture(9, 123, 2);
    assertTrue(Protocol.valid(b, 1));
    assertEquals(3, b[15]);
    assertEquals(0, b[6]);
    assertEquals(0, b[14]);
    assertEquals(123, Protocol.u16(b, 16));
    for (int j = 8; j <= 13; j++) assertEquals(0, b[j]);
  }
}
