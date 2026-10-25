package com.jumpingflash.ax12;

import static org.junit.Assert.*;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.*;
import android.content.Intent;
import android.os.*;
import java.util.*;
import org.junit.*;
import org.junit.runner.RunWith;
import org.robolectric.*;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28)
public class BleLinkTest {
  ActivityController<Activity> controller;
  Activity activity;
  BleLink link;
  BluetoothDevice robot;
  List<String> status = new ArrayList<>();

  @Before
  public void open() {
    controller = Robolectric.buildActivity(Activity.class).setup();
    activity = controller.get();
    Shadows.shadowOf(activity.getApplication())
        .grantPermissions(Manifest.permission.ACCESS_FINE_LOCATION);
    BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
    Shadows.shadowOf(adapter).setEnabled(true);
    robot = adapter.getRemoteDevice("02:00:00:00:00:01");
    Shadows.shadowOf(robot).setCreatedBond(true);
    link =
        new BleLink(
            activity,
            new BleLink.Listener() {
              public void status(String s) {
                status.add(s);
              }

              public void found(BluetoothDevice d) {}

              public void telemetry(byte[] b) {}

              public void lost() {}
            });
  }

  @After
  public void close() {
    link.destroy();
    controller.close();
  }

  void bonded(BluetoothDevice d) {
    Shadows.shadowOf(d).setBondState(BluetoothDevice.BOND_BONDED);
    activity.sendBroadcast(
        new Intent(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            .putExtra(BluetoothDevice.EXTRA_DEVICE, d)
            .putExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_BONDED));
    Shadows.shadowOf(Looper.getMainLooper()).idle();
  }

  @Test
  public void acceptingPairingContinuesOriginalConnectionWithoutAnotherTap() {
    link.connect(robot);
    assertTrue(link.pairing());
    assertEquals(0, Shadows.shadowOf(robot).getBluetoothGatts().size());
    bonded(robot);
    assertFalse(link.pairing());
    assertTrue(link.connecting());
    assertEquals(1, Shadows.shadowOf(robot).getBluetoothGatts().size());
    assertFalse(link.ready);
  }

  @Test
  public void pairingMayFinishInAndroidAndContinueOnlyOnReturn() {
    link.connect(robot);
    link.pause();
    bonded(robot);
    assertTrue(link.pairing());
    assertFalse(link.connecting());
    assertEquals(0, Shadows.shadowOf(robot).getBluetoothGatts().size());
    link.resume();
    assertFalse(link.pairing());
    assertTrue(link.connecting());
  }

  @Test
  public void disconnectCancelsPendingPairingAndLateBondDoesNotConnect() {
    link.connect(robot);
    link.disconnect();
    bonded(robot);
    link.resume();
    assertFalse(link.pairing());
    assertFalse(link.connecting());
    assertEquals(0, Shadows.shadowOf(robot).getBluetoothGatts().size());
  }

  @Test
  public void unrelatedBondAndCancelledPairingNeverConnect() {
    link.connect(robot);
    BluetoothDevice other =
        BluetoothAdapter.getDefaultAdapter().getRemoteDevice("02:00:00:00:00:02");
    bonded(other);
    assertTrue(link.pairing());
    assertFalse(link.connecting());
    link.bondChanged(robot, BluetoothDevice.BOND_NONE);
    assertFalse(link.pairing());
    link.resume();
    assertFalse(link.connecting());
    assertTrue(status.get(status.size() - 1).contains("cancelled"));
  }

  @Test
  public void scanOnlyRequestsCompatibleServiceAndStopsCleanly() {
    link.scan();
    assertTrue(link.scanning);
    org.robolectric.shadows.ShadowBluetoothLeScanner scanner =
        Shadows.shadowOf(BluetoothAdapter.getDefaultAdapter().getBluetoothLeScanner());
    assertEquals(1, scanner.getActiveScans().size());
    assertEquals(
        new ParcelUuid(Protocol.SERVICE),
        scanner.getActiveScans().get(0).scanFilters().get(0).getServiceUuid());
    link.stopScan();
    assertFalse(link.scanning);
    assertEquals(0, scanner.getActiveScans().size());
  }
}
