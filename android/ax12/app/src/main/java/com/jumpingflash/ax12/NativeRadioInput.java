package com.jumpingflash.ax12;

import android.os.SystemClock;

/** Continuous read-only internal inputs. A complete CRC8 source report is the heartbeat. */
public final class NativeRadioInput {
  public static final class LivePort extends NativeUartProbe.DevicePort {
    @Override
    public int open(String path) {
      return "/dev/ttyS0".equals(path) ? NativePort.openLive() : -android.system.OsConstants.EINVAL;
    }
  }

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
  private volatile boolean portReady = false;
  private volatile long bytesRead = 0, accepted = 0, rejected = 0;
  private volatile long firstAcceptedAt = -1,
      lastAcceptedAt = -1,
      maxGapMs = 0,
      longGaps = 0,
      startedAt = 0;
  private volatile int crcFrames = 0, badFrames = 0, discarded = 0;
  private volatile String portDetails = "Not opened";
  private volatile String status = "Stopped";

  public NativeRadioInput(NativeUartProbe.Port p) {
    port = p;
  }

  public boolean running() {
    return running;
  }

  public String status() {
    Sample s = sample();
    if (!running || stop || !portReady) return status;
    if (s != null) {
      long age = SystemClock.elapsedRealtime() - s.at;
      if (s.fresh(SystemClock.elapsedRealtime()))
        return "LIVE • verified radio reports • "
            + accepted
            + " accepted • last "
            + age
            + " ms ago";
      return "STALE • last valid radio report " + age + " ms ago • motor commands blocked";
    }
    if (rejected > 0)
      return "Waiting for valid radio reports • " + rejected + " rejected • motor commands blocked";
    return bytesRead == 0
        ? "Port initialized at 921600 / raw 8N1 • waiting for radio data • NOT LIVE"
        : "Receiving serial bytes, but no valid radio inputs yet • NOT LIVE";
  }

  public String details() {
    return "Native serial "
        + portDetails
        + "\nbytes="
        + bytesRead
        + " accepted="
        + accepted
        + " rejected="
        + rejected
        + " max_update_gap_ms="
        + maxGapMs
        + " gaps_ge_120ms="
        + longGaps
        + " first_report_ms="
        + firstAcceptedAt
        + " last_report_ms="
        + lastAcceptedAt
        + " startup_ms="
        + (firstAcceptedAt < 0 ? -1 : firstAcceptedAt - startedAt)
        + " current_gap_ms="
        + (lastAcceptedAt < 0 ? -1 : SystemClock.elapsedRealtime() - lastAcceptedAt)
        + " crc_frames="
        + crcFrames
        + " bad_frames="
        + badFrames
        + " discarded_bytes="
        + discarded
        + "\n"
        + status();
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
    portReady = false;
    bytesRead = accepted = rejected = 0;
    firstAcceptedAt = lastAcceptedAt = -1;
    maxGapMs = longGaps = 0;
    startedAt = SystemClock.elapsedRealtime();
    crcFrames = badFrames = discarded = 0;
    portDetails = "Not opened";
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
    status = "Stopped";
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
        status = "Serial initialization failed: " + port.error(fd) + " • inputs blocked";
        return;
      }
      portDetails = port.info(fd);
      portReady = true;
      String runningStatus = "Waiting for valid radio reports";
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
        bytesRead += n;
        long now = SystemClock.elapsedRealtime();
        int before = decoder.channelCandidates;
        decoder.feed(b, n, now);
        crcFrames = decoder.crcFrames;
        badFrames = decoder.badFrames;
        discarded = decoder.discarded;
        if (decoder.channelCandidates != before) {
          if (decoder.exactLiveLayout && valid(decoder.sources)) {
            if (lastAcceptedAt >= 0) {
              long gap = now - lastAcceptedAt;
              maxGapMs = Math.max(maxGapMs, gap);
              if (gap >= STALE_MS) longGaps++;
            }
            if (firstAcceptedAt < 0) firstAcceptedAt = now;
            lastAcceptedAt = now;
            accepted++;
            latest = new Sample(decoder.sources, now, decoder.channelCandidates);
            status = runningStatus;
          } else {
            latest = null;
            rejected++;
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
        portReady = false;
        running = false;
      }
    }
  }
}
