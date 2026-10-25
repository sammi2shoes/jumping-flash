package com.jumpingflash.ax12;

import java.util.UUID;

/** Fixed 20-byte LE frames; usable at the default ATT MTU. No implicit enables. */
public final class Protocol {
  public static final UUID SERVICE = UUID.fromString("de88c000-7865-4ce4-9a13-611fad7e0001");
  public static final UUID COMMAND = UUID.fromString("de88c001-7865-4ce4-9a13-611fad7e0001");
  public static final UUID TELEMETRY = UUID.fromString("de88c002-7865-4ce4-9a13-611fad7e0001");
  public static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

  public static int u16(byte[] b, int n) {
    return (b[n] & 255) | ((b[n + 1] & 255) << 8);
  }

  public static int s16(byte[] b, int n) {
    return (short) u16(b, n);
  }

  public static void put(byte[] b, int n, int v) {
    b[n] = (byte) v;
    b[n + 1] = (byte) (v >> 8);
  }

  public static int crc(byte[] b, int length) {
    int c = 0xffff;
    for (int i = 0; i < length; i++) {
      c ^= (b[i] & 255) << 8;
      for (int j = 0; j < 8; j++) c = ((c & 0x8000) != 0 ? (c << 1) ^ 0x1021 : c << 1) & 0xffff;
    }
    return c;
  }

  public static byte[] command(
      int sequence,
      int session,
      boolean armed,
      int mode,
      float forward,
      float turn,
      boolean launch) {
    armed = armed && Float.isFinite(forward) && Float.isFinite(turn) && mode >= 0 && mode <= 2;
    byte[] b = new byte[20];
    b[0] = 74;
    b[1] = 70;
    b[2] = 2;
    b[3] = 1;
    put(b, 4, sequence);
    b[6] = (byte) ((armed ? 1 : 0) | (launch && armed ? 2 : 0));
    b[7] = (byte) mode;
    put(b, 8, armed ? Math.round(Math.max(-1, Math.min(1, forward)) * 1000) : 0);
    put(b, 10, armed ? Math.round(Math.max(-1, Math.min(1, turn)) * 1000) : 0);
    put(b, 16, session);
    put(b, 18, crc(b, 18));
    return b;
  }

  public static byte[] radioCommand(
      int sequence,
      int session,
      boolean armed,
      int sb,
      int style,
      float forward,
      float turn,
      float throttle,
      float height,
      boolean spinnerQualified,
      boolean fullJump) {
    armed = armed && sb > 0 && sb <= 2 && Float.isFinite(height);
    byte[] b = command(sequence, session, armed, style, forward, turn, fullJump);
    boolean drive = (b[6] & 1) != 0;
    boolean spinner = drive && sb == 2 && spinnerQualified && Float.isFinite(throttle);
    if (spinner) b[6] |= 4;
    b[12] = (byte) (spinner ? Math.round(Math.max(0, Math.min(1, throttle)) * 255) : 0);
    b[13] = (byte) (drive ? Math.round(Math.max(0, Math.min(1, height)) * 255) : 0);
    b[14] = (byte) (sb >= 0 && sb <= 2 ? sb : 0);
    b[15] = 0;
    put(b, 18, crc(b, 18));
    return b;
  }

  public static boolean valid(byte[] b, int type) {
    return b != null
        && b.length == 20
        && b[0] == 74
        && b[1] == 70
        && b[2] == 2
        && (b[3] & 255) == type
        && u16(b, 18) == crc(b, 18);
  }

  private Protocol() {}
}
