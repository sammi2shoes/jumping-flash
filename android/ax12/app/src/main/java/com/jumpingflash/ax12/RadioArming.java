package com.jumpingflash.ax12;

/** Switch-edge qualification only. Never enables an actuator by itself. */
public final class RadioArming {
  private int previousSB = -1, previousSC = -1, previousSD = -1;
  private boolean observedOff = false;
  public boolean armEdge = false, jumpEdge = false, spinnerLowQualified = false;

  public void reset() {
    previousSB = previousSC = previousSD = -1;
    observedOff = false;
    armEdge = jumpEdge = spinnerLowQualified = false;
  }

  public void observe(RadioProfile.Snapshot s, boolean armConditions, boolean driveArmed) {
    armEdge = jumpEdge = false;
    boolean stable = s.sb == previousSB && s.sc == previousSC;
    if (s.sb == 0) {
      observedOff = true;
      spinnerLowQualified = false;
    }
    if (s.sb < 0 || s.sc < 0) {
      reset();
      return;
    }
    if (s.sb != 2) spinnerLowQualified = false;
    if (s.sb != previousSB) {
      if (s.sb == 2)
        spinnerLowQualified = Float.isFinite(s.throttle) && s.throttle <= .03f && armConditions;
      if (s.sb > 0
          && previousSB == 0
          && observedOff
          && armConditions
          && (s.sb != 2 || spinnerLowQualified)) armEdge = true;
      if (s.sb > 0) observedOff = false;
    }
    if (s.sd >= 0 && previousSD >= 0 && s.sd != previousSD && stable && s.sb > 0 && driveArmed)
      jumpEdge = true;
    previousSB = s.sb;
    previousSC = s.sc;
    previousSD = s.sd;
  }
}
