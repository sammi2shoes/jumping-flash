package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import java.util.*;
import org.junit.Test;

public class RadioTest {
  private RadioProfile.Snapshot snapshot(int sb, int sc, int sd, float throttle) {
    RadioProfile.Snapshot s = new RadioProfile.Snapshot();
    s.sb = sb;
    s.sc = sc;
    s.sd = sd;
    s.throttle = throttle;
    s.aileron = s.elevator = 0;
    return s;
  }

  @Test
  public void neitherBootNorReconnectArmsFromLatchedSwitch() {
    RadioArming g = new RadioArming();
    g.observe(snapshot(2, 0, 1, 0), true, false);
    assertFalse(g.armEdge);
    assertFalse(g.jumpEdge);
    g.observe(snapshot(0, 0, 1, 0), true, false);
    g.observe(snapshot(1, 0, 1, 0), true, false);
    assertTrue(g.armEdge);
    g.reset();
    g.observe(snapshot(1, 0, 0, 0), true, false);
    assertFalse(g.armEdge);
    assertFalse(g.jumpEdge);
  }

  @Test
  public void bothSdEdgesJumpInEitherArmedStateAndAllStyles() {
    for (int sb : new int[] {1, 2})
      for (int sc = 0; sc < 3; sc++) {
        RadioArming g = new RadioArming();
        g.observe(snapshot(sb, sc, 0, 0), true, true);
        assertFalse(g.jumpEdge);
        g.observe(snapshot(sb, sc, 1, 0), true, true);
        assertTrue(g.jumpEdge);
        g.observe(snapshot(sb, sc, 1, 0), true, true);
        assertFalse(g.jumpEdge);
        g.observe(snapshot(sb, sc, 0, 0), true, true);
        assertTrue(g.jumpEdge);
      }
  }

  @Test
  public void blockedEdgesAreDiscardedNotReplayed() {
    RadioArming g = new RadioArming();
    g.observe(snapshot(0, 0, 0, 0), true, false);
    g.observe(snapshot(0, 0, 1, 0), true, false);
    assertFalse(g.jumpEdge);
    g.observe(snapshot(1, 0, 1, 0), true, false);
    assertTrue(g.armEdge);
    assertFalse(g.jumpEdge);
    g.observe(snapshot(1, 0, 1, 0), true, true);
    assertFalse(g.jumpEdge);
    g.observe(snapshot(1, 1, 0, 0), true, true);
    assertFalse(g.jumpEdge);
  }

  @Test
  public void throttleMustBeLowOnFullArmEntry() {
    RadioArming g = new RadioArming();
    g.observe(snapshot(0, 0, 0, 1), true, false);
    g.observe(snapshot(2, 0, 0, 1), true, false);
    assertFalse(g.armEdge);
    assertFalse(g.spinnerLowQualified);
    g.observe(snapshot(2, 0, 0, 0), true, false);
    assertFalse(g.armEdge);
    assertFalse(g.spinnerLowQualified);
    g.observe(snapshot(0, 0, 0, 0), true, false);
    g.observe(snapshot(2, 0, 0, 0), true, false);
    assertTrue(g.armEdge);
    assertTrue(g.spinnerLowQualified);
    g.observe(snapshot(1, 0, 0, 1), true, true);
    assertFalse(g.spinnerLowQualified);
    g.observe(snapshot(2, 0, 0, 1), true, true);
    assertFalse(g.spinnerLowQualified);
  }

  @Test
  public void invalidSwitchOrFailedConditionsCannotArm() {
    RadioArming g = new RadioArming();
    g.observe(snapshot(0, 0, 0, 0), true, false);
    g.observe(snapshot(1, 0, 0, 0), false, false);
    assertFalse(g.armEdge);
    g.observe(snapshot(1, 0, 0, 0), true, false);
    assertFalse(g.armEdge);
    g.observe(snapshot(-1, 0, 0, 0), true, false);
    g.observe(snapshot(1, 0, 0, 0), true, false);
    assertFalse(g.armEdge);
  }

  @Test
  public void distinctAxesCalibrationAndNormalizedMix() {
    RadioProfile p = new RadioProfile();
    assertFalse(p.driveCalibrated());
    p.binding[1].captured = p.binding[2].captured = 7;
    assertTrue(p.driveCalibrated());
    p.binding[1].axis = p.binding[2].axis = 0;
    assertFalse(p.distinct());
    p.binding[2].axis = 1;
    assertTrue(p.distinct());
    assertArrayEquals(new float[] {1, 0}, RadioProfile.mixPreview(1, 1), .0001f);
  }

  @Test
  public void axisAndKeySwitchBindingsHandleUnknownAndInvalidStates() {
    RadioProfile.Binding b = new RadioProfile.Binding();
    Map<Integer, Float> axes = new HashMap<>();
    Set<Integer> keys = new HashSet<>(), seen = new HashSet<>();
    b.axis = 7;
    assertTrue(Float.isNaN(b.sample(axes, keys, seen)));
    axes.put(7, -1f);
    assertEquals(0, b.sample(axes, keys, seen), 0);
    axes.put(7, 1f);
    assertEquals(1, b.sample(axes, keys, seen), 0);
    b.inverted = true;
    assertEquals(0, b.sample(axes, keys, seen), 0);
    b.axis = -1;
    b.inverted = false;
    b.lowKey = 1;
    b.highKey = 2;
    assertTrue(Float.isNaN(b.sample(axes, keys, seen)));
    keys.add(1);
    assertEquals(0, b.sample(axes, keys, seen), 0);
    seen.add(1);
    keys.clear();
    assertEquals(.5f, b.sample(axes, keys, seen), 0);
    keys.add(2);
    assertEquals(1, b.sample(axes, keys, seen), 0);
    keys.add(1);
    assertTrue(Float.isNaN(b.sample(axes, keys, seen)));
  }

  @Test
  public void threePositionHysteresisAndDeadzone() {
    assertEquals(-1, RadioProfile.threePosition(Float.NaN, 1));
    assertEquals(0, RadioProfile.threePosition(.36f, 0));
    assertEquals(1, RadioProfile.threePosition(.5f, 0));
    assertEquals(2, RadioProfile.threePosition(.9f, 1));
    RadioProfile.Binding b = new RadioProfile.Binding();
    b.axis = 0;
    Map<Integer, Float> a = new HashMap<>();
    a.put(0, .04f);
    assertEquals(0, b.bipolar(a, Collections.emptySet(), Collections.emptySet(), .08f), 0);
    a.put(0, 1f);
    assertEquals(1, b.bipolar(a, Collections.emptySet(), Collections.emptySet(), .08f), 0);
  }

  @Test
  public void spinnerIsZeroOutsideQualifiedFullArmAndJumpIsIndependent() {
    for (int sb = 0; sb < 3; sb++)
      for (int style = 0; style < 3; style++) {
        byte[] b = Protocol.radioCommand(0, 1, true, sb, style, 1, -1, 1, .75f, true, true);
        assertTrue(Protocol.valid(b, 1));
        assertEquals(sb == 0 ? 0 : 3, sb == 2 ? (b[6] & 3) : b[6]);
        assertEquals(sb == 2 ? 255 : 0, b[12] & 255);
        assertEquals(sb == 0 ? 0 : 191, b[13] & 255);
      }
    byte[] b = Protocol.radioCommand(0, 1, true, 2, 0, 0, 0, 1, .5f, false, true);
    assertEquals(0, b[6] & 4);
    assertEquals(0, b[12]);
    b = Protocol.radioCommand(0, 1, false, 2, 0, 1, 1, 1, 1, true, true);
    assertEquals(0, b[6]);
    assertEquals(0, b[12]);
    assertEquals(0, b[13]);
  }

  @Test
  public void invalidHeightDisarmsAndInvalidThrottleNeverSpins() {
    byte[] b = Protocol.radioCommand(0, 1, true, 2, 0, 1, 1, 1, Float.NaN, true, true);
    assertEquals(0, b[6]);
    b = Protocol.radioCommand(0, 1, true, 2, 0, 1, 1, Float.NaN, .5f, true, true);
    assertEquals(0, b[6] & 4);
    assertEquals(0, b[12]);
  }

  @org.junit.Test
  public void dualLegBenchMixSelectsSideAndClampsCombinedRates() {
    org.junit.Assert.assertArrayEquals(new float[] {1, 0}, RadioProfile.legBenchMix(0, -1), .0001f);
    org.junit.Assert.assertArrayEquals(new float[] {0, 1}, RadioProfile.legBenchMix(0, 1), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {-1, -1}, RadioProfile.legBenchMix(-1, 0), .0001f);
    org.junit.Assert.assertArrayEquals(new float[] {1, 1}, RadioProfile.legBenchMix(1, 1), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {-1, 0}, RadioProfile.legBenchMix(-1, 1), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {0, 0}, RadioProfile.legBenchMix(0, .08f), .0001f);
  }

  @org.junit.Test
  public void scMixesDifferentialAndSideSelectionUseBothCh1AndCh2() {
    org.junit.Assert.assertArrayEquals(
        new float[] {1, -1}, RadioProfile.legBenchMix(0, -1, 1), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {0, -1}, RadioProfile.legBenchMix(-1, -1, 1), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {-.5f, 0}, RadioProfile.legBenchMix(-.5f, -1, 2), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {0, .5f}, RadioProfile.legBenchMix(.5f, 1, 2), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {.5f, .5f}, RadioProfile.legBenchMix(.5f, .25f, 2), .0001f);
    org.junit.Assert.assertArrayEquals(
        new float[] {0, 0}, RadioProfile.legBenchMix(0, 1, 2), .0001f);
  }
}
