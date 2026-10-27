package com.jumpingflash.ax12;

import android.system.*;
import java.io.File;
import java.util.Locale;

/**
 * Read-only serial bytes; live input configures raw921600/8N1, passive capture preserves termios.
 */
public final class NativePort {
  private static boolean attempted = false, loaded = false;
  private static String loadError = "not loaded";

  public static synchronized boolean available() {
    if (!attempted) {
      attempted = true;
      try {
        System.loadLibrary("jf_uart_probe");
        loaded = true;
        loadError = "loaded";
      } catch (UnsatisfiedLinkError | SecurityException e) {
        loadError = e.toString();
      }
    }
    return loaded;
  }

  public static String availability() {
    available();
    return loadError;
  }

  public static native int openExclusive(String path);

  public static native int openLive();

  public static native String describeOpen(int fd);

  public static native int readBytes(int fd, byte[] data);

  public static native void closePort(int fd);

  public static String error(int code) {
    return code < 0 ? Os.strerror(-code) : "OK";
  }

  private static boolean access(String path, int mask) {
    try {
      return Os.access(path, mask);
    } catch (ErrnoException e) {
      return false;
    }
  }

  public static String describe(String path) {
    try {
      StructStat s = Os.stat(path);
      return String.format(
          Locale.US,
          "%s: mode=%o uid=%d gid=%d char_device=%s read=%s write=%s canonical=%s",
          path,
          s.st_mode,
          s.st_uid,
          s.st_gid,
          OsConstants.S_ISCHR(s.st_mode),
          access(path, OsConstants.R_OK),
          access(path, OsConstants.W_OK),
          new File(path).getCanonicalPath());
    } catch (Exception e) {
      return path + ": " + e.getClass().getSimpleName() + " " + e.getMessage();
    }
  }

  private NativePort() {}
}
