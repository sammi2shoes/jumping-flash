package com.jumpingflash.ax12;

import java.util.*;

/** Bounded diagnostic framing. Candidates never become robot control inputs. */
public final class UmbusProbe {
  public int frames = 0,
      crcFrames = 0,
      xorFrames = 0,
      badFrames = 0,
      discarded = 0,
      channelCandidates = 0;
  public long lastCandidateAt = 0, firstCandidateAt = 0;
  public short[] sources = null;
  public boolean exactLiveLayout = false;
  public String lastFrame = "none", lastChecksum = "none";
  private byte[] buffer = new byte[2048];
  private int used = 0;

  public static int crc8(byte[] b, int first, int end) {
    int crc = 0;
    for (int p = first; p < end; p++) {
      crc ^= b[p] & 255;
      for (int k = 0; k < 8; k++) crc = (crc & 1) != 0 ? (crc >>> 1) ^ 0x8c : crc >>> 1;
    }
    return crc;
  }

  private int u(int i) {
    return buffer[i] & 255;
  }

  private void drop(int n) {
    System.arraycopy(buffer, n, buffer, 0, used - n);
    used -= n;
  }

  public void feed(byte[] data, int count, long now) {
    if (count < 0 || count > data.length) throw new IllegalArgumentException();
    for (int i = 0; i < count; i++) {
      if (used == buffer.length) {
        drop(1);
        discarded++;
      }
      buffer[used++] = data[i];
      parse(now);
    }
  }

  private void parse(long now) {
    while (used >= 2) {
      int sync = u(0);
      if (sync != 0xa3 && sync != 0xa6 && sync != 0xa7) {
        drop(1);
        discarded++;
        continue;
      }
      if (sync == 0xa7 && used < 3) return;
      int header = sync == 0xa3 ? 3 : sync == 0xa6 ? 5 : 6;
      int length = sync == 0xa7 ? u(1) | (u(2) << 8) : u(1);
      if (length < header + 1 || length > 1024) {
        drop(1);
        badFrames++;
        continue;
      }
      if (used < length) return;
      int crc = crc8(buffer, 1, length - 1), xor = 0;
      for (int i = 1; i < length - 1; i++) xor ^= u(i);
      boolean crcOK = crc == u(length - 1), xorOK = xor == u(length - 1);
      if (!crcOK && !xorOK) {
        drop(1);
        badFrames++;
        continue;
      }
      frames++;
      if (crcOK) crcFrames++;
      else xorFrames++;
      lastChecksum = crcOK ? "CRC8" : "XOR-only";
      lastFrame = hex(buffer, 0, length);
      int route = sync == 0xa6 ? 2 : 3;
      // Sept-build native receiver: RC group0x10, source2, destination4,
      // subtype1 followed by42 signed16 raw sources plus metadata (93byte payload).
      // This is an explicitly unvalidated live-layout candidate, never armed input.
      if (crcOK
          && sync != 0xa3
          && length >= header + 94
          && u(route) == 0x10
          && u(route + 1) == 2
          && u(route + 2) == 4
          && u(header) == 1) {
        short[] values = new short[42];
        for (int k = 0; k < 42; k++) {
          int off = header + 1 + k * 2;
          values[k] = (short) (u(off) | (u(off + 1) << 8));
        }
        exactLiveLayout = sync == 0xa6 && length == 99;
        sources = values;
        channelCandidates++;
        lastCandidateAt = now;
        if (firstCandidateAt == 0) firstCandidateAt = now;
      }
      drop(length);
    }
  }

  public static String hex(byte[] b, int first, int count) {
    if (first < 0 || count < 0 || first > b.length - count) throw new IllegalArgumentException();
    final String digits = "0123456789ABCDEF";
    StringBuilder s = new StringBuilder(count * 2);
    for (int i = first; i < first + count; i++) {
      int v = b[i] & 255;
      s.append(digits.charAt(v >>> 4)).append(digits.charAt(v & 15));
    }
    return s.toString();
  }

  public String describe(long now) {
    StringBuilder s =
        new StringBuilder(
            String.format(
                Locale.US,
                "Frames=%d CRC8=%d XOR-only=%d rejected=%d discarded_bytes=%d\n"
                    + "Sept-layout candidates=%d age=%s\n",
                frames,
                crcFrames,
                xorFrames,
                badFrames,
                discarded,
                channelCandidates,
                lastCandidateAt == 0 ? "none" : (now - lastCandidateAt) + " ms"));
    if (channelCandidates > 1 && lastCandidateAt > firstCandidateAt)
      s.append(
          String.format(
              Locale.US,
              "Candidate report rate: %.1f Hz\n",
              (channelCandidates - 1) * 1000.0 / (lastCandidateAt - firstCandidateAt)));
    if (sources != null) {
      String[] names = {"RUD", "ELE / CH2", "THR / CH3", "AIL / CH1", "S1", "S2", "SB", "SC", "SD"};
      int[] ids = {0, 1, 2, 3, 12, 13, 21, 22, 23};
      for (int i = 0; i < ids.length; i++)
        s.append(names[i])
            .append(" [")
            .append(ids[i])
            .append("] raw=")
            .append(sources[ids[i]] == Short.MIN_VALUE ? "unavailable" : sources[ids[i]])
            .append('\n');
    }
    return s.append("Last ").append(lastChecksum).append(" frame: ").append(lastFrame).toString();
  }
}
