package com.jumpingflash.ax12;

import android.os.SystemClock;

/** Continuous read-only internal inputs. A complete CRC8 source report is the heartbeat. */
public final class NativeRadioInput {
  public static final int[] SOURCES = {2, 3, 1, 13, 21, 22, 23};
  public static final int AXIS_BASE = 1000;
  public static final long STALE_MS = 120;

  public static final class Sample {
    public final short[] values;
    public final long at, number;

    Sample(short[] v, long at, long number) {
      this.values = v.clone();
      this.at = at;
      this.number = number;
    }

    public boolean fresh(long now) {
      return now >= at && now - at < STALE_MS;
    }
  }

  private final NativeUartProbe.Port port;
  private volatile boolean running, stop;
  private volatile Sample latest;
  private volatile String status = "Stopped";

  public NativeRadioInput(NativeUartProbe.Port p) {
    port = p;
  }

  public boolean running() {
    return running;
  }

  public String status() {
    return status;
  }

  public Sample sample() {
    return running && !stop ? latest : null;
  }

  public static boolean valid(short[] values) {
    if (values == null || values.length != 42) return false;
    for (int i : SOURCES) if (values[i] < -550 || values[i] > 550) return false;
    // Real switches are -500/0/+500 (SB/SC) and -500/+500 (SD).
    for (int i : new int[] {21, 22})
      if (Math.abs(values[i]) > 50 && Math.abs(Math.abs(values[i]) - 500) > 50) return false;
    return Math.abs(Math.abs(values[23]) - 500) <= 50;
  }

  public synchronized boolean start(boolean ownerConfirmed) {
    if (running || !ownerConfirmed) return false;
    latest = null;
    stop = false;
    running = true;
    status = "Opening internal input reader…";
    Thread worker = new Thread(this::read, "jf-native-radio");
    worker.setDaemon(true);
    worker.start();
    return true;
  }

  public void stop() {
    stop = true;
    latest = null;
  }

  private void read() {
    int fd = -1;
    try {
      if (!port.available()) {
        status = "Reader unavailable: " + port.availability();
        return;
      }
      if (stop) return;
      fd = port.open("/dev/ttyS0");
      if (fd < 0) {
        status = "Open failed: " + port.error(fd);
        return;
      }
      String runningStatus = "Internal inputs running; " + port.info(fd);
      status = runningStatus;
      byte[] b = new byte[512];
      UmbusProbe decoder = new UmbusProbe();
      while (!stop) {
        int n = port.read(fd, b);
        if (n < 0 || n > b.length) {
          status = "Read failed: " + port.error(n);
          return;
        }
        if (n == 0) continue;
        long now = SystemClock.elapsedRealtime();
        int before = decoder.channelCandidates;
        decoder.feed(b, n, now);
        if (decoder.channelCandidates != before) {
          if (decoder.exactLiveLayout && valid(decoder.sources)) {
            latest = new Sample(decoder.sources, now, decoder.channelCandidates);
            status = runningStatus;
          } else {
            latest = null;
            status = "Waiting for a valid source report; outputs blocked";
          }
        }
      }
      status = "Stopped";
    } catch (Exception | LinkageError e) {
      status = "Reader failed: " + e.getMessage();
    } finally {
      try {
        if (fd >= 0) port.close(fd);
      } finally {
        latest = null;
        running = false;
      }
    }
  }
}
