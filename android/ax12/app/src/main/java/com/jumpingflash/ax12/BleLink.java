package com.jumpingflash.ax12;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.pm.PackageManager;
import android.os.*;
import java.util.*;

/** All state and callbacks are confined to the main looper. One acknowledged write in flight. */
public final class BleLink {
  public interface Listener {
    void status(String s);

    void found(BluetoothDevice d);

    void telemetry(byte[] b);

    void lost();
  }

  private final Activity activity;
  private final Listener listener;
  private final Handler handler = new Handler(Looper.getMainLooper());
  private BluetoothGatt gatt;
  private BluetoothGattCharacteristic command;
  private BluetoothLeScanner scanner;
  public boolean ready = false, busy = false;
  public long writeAt = 0;
  private int generation = 0;
  private int scanGeneration = 0;

  public BleLink(Activity a, Listener l) {
    activity = a;
    listener = l;
  }

  public boolean permissions() {
    String[] wanted =
        Build.VERSION.SDK_INT >= 31
            ? new String[] {
              Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT
            }
            : new String[] {Manifest.permission.ACCESS_FINE_LOCATION};
    for (String p : wanted)
      if (activity.checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
        activity.requestPermissions(wanted, 10);
        return false;
      }
    return true;
  }

  private BluetoothAdapter adapter() {
    BluetoothManager m = (BluetoothManager) activity.getSystemService(Activity.BLUETOOTH_SERVICE);
    return m == null ? null : m.getAdapter();
  }

  private final ScanCallback scanCallback =
      new ScanCallback() {
        @Override
        public void onScanResult(int type, ScanResult r) {
          handler.post(() -> listener.found(r.getDevice()));
        }

        @Override
        public void onScanFailed(int error) {
          handler.post(() -> listener.status("Scan failed: " + error));
        }
      };

  public void scan() {
    if (!permissions()) return;
    stopScan();
    BluetoothAdapter a = adapter();
    if (a == null || !a.isEnabled()) {
      listener.status("Enable Bluetooth in Android settings, then scan again.");
      return;
    }
    scanner = a.getBluetoothLeScanner();
    if (scanner == null) {
      listener.status("BLE scanning unavailable.");
      return;
    }
    try {
      scanner.startScan(
          null,
          new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
          scanCallback);
      listener.status("Scanning BLE devices for 10 seconds…");
      int scanToken = scanGeneration;
      handler.postDelayed(
          () -> {
            if (scanToken == scanGeneration) {
              stopScan();
              listener.status("Scan finished. Choose a discovered device.");
            }
          },
          10000);
    } catch (SecurityException | IllegalStateException e) {
      listener.status("Scan unavailable: " + e.getMessage());
    }
  }

  public void stopScan() {
    ++scanGeneration;
    if (scanner != null) {
      try {
        scanner.stopScan(scanCallback);
      } catch (SecurityException | IllegalStateException ignored) {
      }
      scanner = null;
    }
  }

  public void connect(BluetoothDevice device) {
    if (!permissions()) return;
    disconnect();
    stopScan();
    int token = ++generation;
    listener.status("Connecting; motor commands remain disarmed.");
    try {
      if (device.getBondState() != BluetoothDevice.BOND_BONDED) {
        if (device.getBondState() == BluetoothDevice.BOND_NONE && !device.createBond()) {
          listener.status("Pairing could not start. Pair JumpingFlash-Leg in Bluetooth settings.");
        } else
          listener.status(
              "Complete Android pairing, then choose Connect again. No automatic connection or"
                  + " arm.");
        return;
      }
      gatt =
          device.connectGatt(
              activity,
              false,
              new BluetoothGattCallback() {
                private void post(BluetoothGatt g, Runnable r) {
                  handler.post(
                      () -> {
                        if (token == generation && g == gatt) r.run();
                      });
                }

                @Override
                public void onConnectionStateChange(BluetoothGatt g, int status, int state) {
                  post(
                      g,
                      () -> {
                        if (status == BluetoothGatt.GATT_SUCCESS
                            && state == BluetoothProfile.STATE_CONNECTED) {
                          g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                          listener.status("Discovering Jumping Flash service…");
                          if (!g.discoverServices()) fail("Service discovery could not start.");
                        } else fail("Bluetooth disconnected (" + status + "). Reconnect manually.");
                      });
                }

                @Override
                public void onServicesDiscovered(BluetoothGatt g, int status) {
                  post(
                      g,
                      () -> {
                        if (status != BluetoothGatt.GATT_SUCCESS) {
                          fail("Service discovery failed.");
                          return;
                        }
                        BluetoothGattService s = g.getService(Protocol.SERVICE);
                        if (s == null) {
                          fail(
                              "No Jumping Flash BLE service. Current Bluepad32 firmware is not"
                                  + " compatible.");
                          return;
                        }
                        command = s.getCharacteristic(Protocol.COMMAND);
                        BluetoothGattCharacteristic t = s.getCharacteristic(Protocol.TELEMETRY);
                        if (command == null
                            || (command.getProperties()
                                    & BluetoothGattCharacteristic.PROPERTY_WRITE)
                                == 0
                            || t == null
                            || (t.getProperties() & BluetoothGattCharacteristic.PROPERTY_NOTIFY)
                                == 0) {
                          fail("Required command/telemetry properties missing.");
                          return;
                        }
                        BluetoothGattDescriptor d = t.getDescriptor(Protocol.CCCD);
                        if (d == null || !g.setCharacteristicNotification(t, true)) {
                          fail("Cannot subscribe to robot status.");
                          return;
                        }
                        d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                        if (!g.writeDescriptor(d)) fail("Cannot request telemetry subscription.");
                      });
                }

                @Override
                public void onDescriptorWrite(
                    BluetoothGatt g, BluetoothGattDescriptor d, int status) {
                  post(
                      g,
                      () -> {
                        if (status != BluetoothGatt.GATT_SUCCESS) {
                          fail("Telemetry subscription rejected.");
                          return;
                        }
                        ready = true;
                        listener.status(
                            "BLE ready; waiting for matching robot telemetry. No automatic arm.");
                      });
                }

                @Override
                public void onCharacteristicChanged(
                    BluetoothGatt g, BluetoothGattCharacteristic c) {
                  byte[] copy = c.getValue() == null ? null : c.getValue().clone();
                  post(
                      g,
                      () -> {
                        if (Protocol.TELEMETRY.equals(c.getUuid())) listener.telemetry(copy);
                      });
                }

                @Override
                public void onCharacteristicWrite(
                    BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
                  post(
                      g,
                      () -> {
                        busy = false;
                        if (status != BluetoothGatt.GATT_SUCCESS)
                          fail("Command write rejected: " + status);
                      });
                }
              },
              BluetoothDevice.TRANSPORT_LE);
      if (gatt == null) fail("Bluetooth connection unavailable.");
      final int attempt = generation;
      handler.postDelayed(
          () -> {
            if (attempt == generation && !ready) fail("Connection setup timed out.");
          },
          10000);
    } catch (SecurityException | IllegalArgumentException e) {
      fail("Connection failed: " + e.getMessage());
    }
  }

  public boolean send(byte[] frame) {
    if (!ready || busy || gatt == null || command == null) return false;
    command.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
    command.setValue(frame);
    busy = true;
    writeAt = SystemClock.elapsedRealtime();
    try {
      if (!gatt.writeCharacteristic(command)) {
        fail("Cannot submit command.");
        return false;
      }
      return true;
    } catch (SecurityException | IllegalStateException e) {
      fail("Command transport failed.");
      return false;
    }
  }

  private void fail(String why) {
    disconnect();
    listener.status(why);
  }

  public void disconnect() {
    ++generation;
    ready = false;
    busy = false;
    command = null;
    BluetoothGatt old = gatt;
    gatt = null;
    if (old != null) {
      try {
        old.disconnect();
        old.close();
      } catch (SecurityException ignored) {
      }
    }
    listener.lost();
  }

  public void close() {
    stopScan();
    disconnect();
  }
}
