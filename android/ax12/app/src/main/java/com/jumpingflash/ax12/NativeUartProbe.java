package com.jumpingflash.ax12;

import android.os.SystemClock;
import java.util.Locale;

/** Explicit, bounded passive capture; owns/cleans its fd on one worker thread. */
public final class NativeUartProbe {
  public interface Port {
    boolean available();

    String availability();

    String describe(String path);

    default String info(int fd) {
      return "Open-port settings unavailable";
    }

    int open(String path);

    default boolean supportsLinkProbe() {
      return false;
    }

    default int linkProbe(int fd) {
      return -android.system.OsConstants.ENOTSUP;
    }

    int read(int fd, byte[] data);

    void close(int fd);

    String error(int code);
  }

  public static class DevicePort implements Port {
    public boolean available() {
      return NativePort.available();
    }

    public String availability() {
      return NativePort.availability();
    }

    public String describe(String p) {
      return NativePort.describe(p);
    }

    public int open(String p) {
      return NativePort.openExclusive(p);
    }

    public String info(int fd) {
      return NativePort.describeOpen(fd);
    }

    public int read(int fd, byte[] b) {
      return NativePort.readBytes(fd, b);
    }

    public void close(int fd) {
      NativePort.closePort(fd);
    }

    public String error(int e) {
      return NativePort.error(e);
    }
  }

  private final Port port;
  private final Runnable changed;
  private final Object lock = new Object();
  private volatile boolean running = false, stop = false;
  private Thread worker;
  private String status = "Not started", metadata = "", path = "/dev/ttyS0";
  private StringBuilder raw = new StringBuilder();
  private UmbusProbe decoder = new UmbusProbe();
  private long began = 0, ended = 0;
  private int bytes = 0;
  public static final int MAX_BYTES = 128 * 1024, MAX_MS = 45000;

  public NativeUartProbe(Port p, Runnable changed) {
    port = p;
    this.changed = changed;
  }

  public boolean running() {
    return running;
  }

  public String metadata() {
    return port.describe("/dev/ttyS0") + "\n" + port.describe("/dev/ttyS4");
  }

  public synchronized boolean start(String selected, boolean ownerConfirmed) {
    if (running) return false;
    if (!ownerConfirmed) {
      synchronized (lock) {
        status = "Blocked: confirm stock RadioMaster app is stopped first.";
      }
      changed.run();
      return false;
    }
    if (!selected.equals("/dev/ttyS0") && !selected.equals("/dev/ttyS4"))
      throw new IllegalArgumentException("Unsupported port");
    stop = false;
    running = true;
    synchronized (lock) {
      path = selected;
      began = SystemClock.elapsedRealtime();
      ended = 0;
      bytes = 0;
      decoder = new UmbusProbe();
      raw = new StringBuilder();
      metadata = metadata();
      status = "Checking reader and access…";
    }
    worker = new Thread(this::capture, "jf-native-uart-probe");
    worker.setDaemon(true);
    worker.start();
    changed.run();
    return true;
  }

  private void state(String s) {
    synchronized (lock) {
      status = s;
    }
    changed.run();
  }

  private void capture() {
    int fd = -1;
    try {
      if (!port.available()) {
        state("Native reader unavailable: " + port.availability());
        return;
      }
      if (stop) return;
      fd = port.open(path);
      if (fd < 0) {
        state(
            "Open/exclusive request failed: "
                + port.error(fd)
                + " (errno="
                + (-fd)
                + "). No root or permission changes attempted.");
        return;
      }
      synchronized (lock) {
        metadata += "\nOPEN_PORT " + port.info(fd);
      }
      state(
          "READ-ONLY CAPTURE: exclusive ioctl set. No writes or baud changes; operator must have"
              + " stopped other readers.");
      byte[] data = new byte[512];
      while (!stop) {
        long now = SystemClock.elapsedRealtime();
        synchronized (lock) {
          if (now - began >= MAX_MS || bytes >= MAX_BYTES) {
            status = "Capture finished at time/size limit.";
            break;
          }
        }
        int n = port.read(fd, data);
        if (n < 0) {
          state("Read ended: " + port.error(n) + " (errno=" + (-n) + ")");
          return;
        }
        if (n > data.length) {
          state("Invalid native read size; stopped.");
          return;
        }
        if (n > 0) {
          now = SystemClock.elapsedRealtime();
          synchronized (lock) {
            int keep = Math.min(n, MAX_BYTES - bytes);
            raw.append("UART_RX t_ms=")
                .append(now)
                .append(" offset=")
                .append(bytes)
                .append(" bytes=")
                .append(keep)
                .append(" hex=")
                .append(UmbusProbe.hex(data, 0, keep))
                .append('\n');
            bytes += keep;
            decoder.feed(data, keep, now);
          }
        }
      }
      if (stop) state("Capture stopped; port closing.");
    } catch (Exception | LinkageError e) {
      state("Probe failed: " + e.getClass().getSimpleName() + " " + e.getMessage());
    } finally {
      try {
        if (fd >= 0) {
          port.close(fd);
          synchronized (lock) {
            status += " Port closed.";
          }
        }
      } catch (Exception | LinkageError e) {
        state("Port close failed: " + e.getMessage());
      } finally {
        synchronized (lock) {
          ended = SystemClock.elapsedRealtime();
        }
        running = false;
        changed.run();
      }
    }
  }

  public void stop() {
    stop = true;
  }

  public boolean awaitStopped(long ms) throws InterruptedException {
    Thread w = worker;
    if (w != null) w.join(ms);
    return !running;
  }

  public String snapshot() {
    synchronized (lock) {
      long now = SystemClock.elapsedRealtime();
      return status
          + "\n"
          + String.format(
              Locale.US,
              "Port=%s bytes=%d elapsed=%d ms running=%s\n",
              path,
              bytes,
              began == 0 ? 0 : (ended > 0 ? ended : now) - began,
              running)
          + decoder.describe(now);
    }
  }

  public String dump() {
    synchronized (lock) {
      return "NATIVE UART PROBE — DIAGNOSTIC ONLY, NO ROBOT OUTPUT\n"
          + metadata
          + "\n"
          + snapshot()
          + "\nRAW SERIAL CAPTURE\n"
          + raw;
    }
  }
}
