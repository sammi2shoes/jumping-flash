package com.jumpingflash.ax12;

import android.app.*;
import android.bluetooth.BluetoothDevice;
import android.content.*;
import android.graphics.*;
import android.hardware.input.InputManager;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;

public final class MainActivity extends Activity implements InputManager.InputDeviceListener {
  private boolean nativeProbeMode = false;
  private boolean nativeLive = false, inputStopSent = false;
  private boolean nativeEnabled = true;
  private Button nativeToggle;
  private TextView nativeLiveStatus;
  private MissionView mission;
  private TextView missionStatus, missionDetails, connectionSteps, missionTitle, driveHint;
  private LinearLayout robotCards;
  private Button disconnectButton;
  private final Map<String, Button> robotButtons = new HashMap<>();
  private final Set<String> shownRobots = new HashSet<>();
  private Runnable pendingBluetoothAction;
  private InputMonitorView inputMonitor;
  private TextView inputMonitorStatus, rawSourcesView;
  private boolean rawSourcesExpanded = false;
  private long monitorAt = 0;

  private static String sourceName(int id) {
    switch (id) {
      case 0:
        return "RUD";
      case 1:
        return "ELE";
      case 2:
        return "THR";
      case 3:
        return "AIL";
      case 12:
        return "S1";
      case 13:
        return "S2";
      case 21:
        return "SB";
      case 22:
        return "SC";
      case 23:
        return "SD";
      default:
        return "Source";
    }
  }

  private void inputMonitorPage() {
    showTab(3);
    body.removeAllViews();
    nativeToggle = null;
    nativeLiveStatus = null;
    body.addView(text("LIVE INPUT MONITOR", 26));
    body.addView(
        text(
            "Keep SB DISARMED while checking inputs. These are raw logical radio sources"
                + " (-500…500); control reversals/calibration are applied separately. Gimbal plots"
                + " show RUD/THR and AIL/ELE, not a forced physical stick mode.",
            14));
    LinearLayout monitorCard = card();
    inputMonitorStatus = text("", 15);
    monitorCard.addView(inputMonitorStatus);
    inputMonitor = new InputMonitorView(this);
    monitorCard.addView(inputMonitor, new LinearLayout.LayoutParams(-1, dp(350)));
    body.addView(monitorCard);
    LinearLayout actions = row();
    addButton(actions, "Retry radio inputs", () -> setNativeEnabled(true));
    addButton(actions, "Back to Pit Tools", () -> showTab(3));
    body.addView(actions);
    body.addView(
        button(
            rawSourcesExpanded ? "Hide all 42 raw sources" : "Show all 42 raw sources",
            () -> {
              rawSourcesExpanded = !rawSourcesExpanded;
              inputMonitorPage();
            }));
    if (rawSourcesExpanded) {
      rawSourcesView = text("", 14);
      rawSourcesView.setTypeface(Typeface.MONOSPACE);
      body.addView(rawSourcesView);
    }
    render();
  }

  private void updateInputMonitor(long now) {
    if (inputMonitor == null) return;
    NativeRadioInput.Sample sample = nativeRadio.sample();
    inputMonitor.update(sample, now);
    if (inputMonitorStatus != null) inputMonitorStatus.setText(nativeRadio.status());
    if (rawSourcesView != null) {
      if (sample == null || !sample.fresh(now)) {
        rawSourcesView.setText("No fresh raw source values. Last-known controls are not live.");
        return;
      }
      StringBuilder text = new StringBuilder();
      for (int i = 0; i < sample.values.length; i++)
        text.append(
            String.format(
                Locale.US,
                "%02d  %-6s  %s%s",
                i,
                sourceName(i),
                sample.values[i] == Short.MIN_VALUE
                    ? "unavailable"
                    : String.format(Locale.US, "%+d", sample.values[i]),
                i % 3 == 2 ? "\n" : "      "));
      rawSourcesView.setText(text.toString());
    }
  }

  private int dp(float value) {
    return (int) (value * getResources().getDisplayMetrics().density + .5f);
  }

  private android.graphics.drawable.GradientDrawable surface(int fill, int stroke, int radius) {
    android.graphics.drawable.GradientDrawable g = new android.graphics.drawable.GradientDrawable();
    g.setColor(fill);
    g.setCornerRadius(dp(radius));
    if (stroke != 0) g.setStroke(dp(1), stroke);
    return g;
  }

  private void paintButton(Button b, int fill, int ink) {
    b.setBackground(surface(fill, 0, 12));
    b.setTextColor(ink);
  }

  private LinearLayout card() {
    LinearLayout c = new LinearLayout(this);
    c.setOrientation(LinearLayout.VERTICAL);
    c.setPadding(dp(14), dp(10), dp(14), dp(10));
    c.setBackground(surface(Color.WHITE, Color.rgb(205, 225, 243), 18));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.setMargins(dp(4), dp(5), dp(4), dp(5));
    c.setLayoutParams(lp);
    return c;
  }

  private boolean prepareBluetooth(Runnable continuation) {
    try {
      pendingBluetoothAction = continuation;
      if (!ble.permissions()) return false;
      android.bluetooth.BluetoothManager manager =
          (android.bluetooth.BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
      android.bluetooth.BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
      if (adapter == null) {
        pendingBluetoothAction = null;
        linkStatus = "Bluetooth is unavailable on this device";
        render();
        return false;
      }
      if (!adapter.isEnabled()) {
        startActivityForResult(
            new Intent(android.bluetooth.BluetoothAdapter.ACTION_REQUEST_ENABLE), 11);
        return false;
      }
      pendingBluetoothAction = null;
      return true;
    } catch (SecurityException denied) {
      pendingBluetoothAction = null;
      linkStatus = "Bluetooth permission was revoked. Tap Find my Stick to grant it again.";
      render();
      return false;
    }
  }

  private void findRobot() {
    if (nativeProbeMode) {
      toast("Exit the passive probe before connecting");
      return;
    }
    if (!prepareBluetooth(this::findRobot)) return;
    android.location.LocationManager lm =
        (android.location.LocationManager) getSystemService(LOCATION_SERVICE);
    if (Build.VERSION.SDK_INT < 31 && lm != null && !lm.isLocationEnabled()) {
      pendingBluetoothAction = this::findRobot;
      linkStatus = "Android needs Location ON for Bluetooth discovery";
      startActivityForResult(
          new Intent(android.provider.Settings.ACTION_LOCATION_SOURCE_SETTINGS), 12);
      return;
    }
    disarm("Looking for Stick; robot remains disabled");
    devices.clear();
    shownRobots.clear();
    robotButtons.clear();
    if (robotCards != null) robotCards.removeAllViews();
    ble.scan();
  }

  private void connectRobot(BluetoothDevice d) {
    if (nativeProbeMode || demo) {
      toast("Return to live inputs before connecting the robot");
      return;
    }
    if (!prepareBluetooth(() -> connectRobot(d))) return;
    disarm("Connecting to Stick; select SB DISARMED before arming");
    telemetryAt = 0;
    telemetrySeq = -1;
    session = 1 + new java.security.SecureRandom().nextInt(65535);
    prefs.edit().putString("lastRobot", d.getAddress()).apply();
    ble.connect(d);
  }

  private void reconnectRobot() {
    if (!prepareBluetooth(this::reconnectRobot)) return;
    String address = prefs.getString("lastRobot", "");
    if (address.isEmpty()) {
      findRobot();
      return;
    }
    android.bluetooth.BluetoothManager m =
        (android.bluetooth.BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
    try {
      connectRobot(m.getAdapter().getRemoteDevice(address));
    } catch (IllegalArgumentException e) {
      prefs.edit().remove("lastRobot").apply();
      findRobot();
    }
  }

  private void showRobotCard(BluetoothDevice d) {
    try {
      if (robotCards == null) return;
      String address = d.getAddress();
      if (!shownRobots.add(address)) return;
      String name = d.getName() == null ? "Jumping Flash Stick" : d.getName();
      LinearLayout c = card();
      c.addView(text(name, 21));
      c.addView(text("Compatible Stick • " + address, 13));
      Button connect = button("Connect this Stick  →", () -> connectRobot(d));
      paintButton(connect, Color.rgb(255, 214, 64), TEXT);
      c.addView(connect);
      robotCards.addView(c);
    } catch (SecurityException denied) {
      linkStatus = "Bluetooth permission was revoked. Tap Find my Stick to grant it again.";
      render();
    }
  }

  private int nativeResumeGeneration = 0;

  private void setNativeEnabled(boolean enabled) {
    nativeEnabled = enabled;
    ++nativeResumeGeneration;
    prefs.edit().putBoolean("nativeEnabled", enabled).apply();
    disarm(
        enabled
            ? "Internal inputs enabled; select SB DISARMED before arming"
            : "Internal inputs OFF");
    if (!enabled) {
      nativeRadio.stop();
      sendFrame();
    } else {
      if (nativeProbe.running()) nativeProbe.stop();
      nativeProbeMode = false;
      prefs.edit().putBoolean("nativeProbeMode", false).apply();
      selectNativeInputs();
      resumeNativeInputs(nativeResumeGeneration, 0);
    }
    render();
  }

  private void resumeNativeInputs(int generation, int attempt) {
    if (!foreground || !nativeEnabled || nativeProbeMode || generation != nativeResumeGeneration)
      return;
    if (nativeRadio.running() || nativeProbe.running()) {
      if (attempt < 25) handler.postDelayed(() -> resumeNativeInputs(generation, attempt + 1), 50);
      return;
    }
    nativeNumber = -1;
    radioState = new RadioProfile.Snapshot();
    disarm("Internal inputs resumed; select SB DISARMED before rearming");
    nativeRadio.start(true);
  }

  private NativeRadioInput nativeRadio;
  private long nativeNumber = -1;

  private boolean liveInputFresh() {
    if (!nativeLive) return InputDevice.getDevice(inputId) != null;
    NativeRadioInput.Sample v = nativeRadio == null ? null : nativeRadio.sample();
    return v != null && v.fresh(SystemClock.elapsedRealtime());
  }

  private boolean directionTest() {
    return (capabilities & (64 | 128)) != 0;
  }

  private boolean dualPosition() {
    return (capabilities & 128) != 0;
  }

  private boolean benchMode() {
    return (capabilities & (32 | 64 | 128)) != 0;
  }

  private int readyMask() {
    return directionTest() && !dualPosition() ? 1 : 3;
  }

  private boolean maintenancePending = false, maintenanceCancelPending = false;
  private String commissioningEvidence = "No commissioning telemetry received";
  private int commissioningFlags = -1;
  private TextView connectionHelp;

  private String selectionName() {
    if (dualPosition()) return "BOTH MOTORS • FIXED TRAVEL";
    return radioState.sc < 0
        ? "Waiting"
        : new String[] {"RIGHT • ID 1", "LEFT • ID 2", "BOTH MOTORS"}[radioState.sc];
  }

  private void requestWatchdogCheck() {
    if (!directionTest()
        || !freshTelemetry()
        || !liveInputFresh()
        || radioState.sb != 0
        || (robotFlags & 4) != 0) {
      toast("Connect direction-test firmware; keep SB DISARMED and center gimbals first");
      return;
    }
    if (Math.abs(forward()) > .02f || Math.abs(turn()) > .02f) {
      toast("Center both gimbals for the zero-speed watchdog check");
      return;
    }
    disarm("Watchdog check requested. Keep SB DISARMED and gimbals centered for about 6 seconds.");
    maintenancePending = true;
    sendFrame();
  }

  private String motorDiagnostic() {
    if (lastFaults == 0) return "none";
    if (!dualPosition()) return "flags 0x" + Integer.toHexString(lastFaults);
    String[] reasons = {
      "unknown",
      "CAN controller / TX",
      "read replies missing",
      "parameter rejected",
      "write verification mismatch",
      "nonfinite encoder",
      "outside fixed travel",
      "encoder continuity lost",
      "motor settings changed",
      "unexpected motor mode",
      "motor fault",
      "motor restart detected",
      "following error",
      "STOP unconfirmed"
    };
    int code = lastFaults & 15;
    return "ID"
        + (lastFaults >>> 4)
        + " "
        + (code < reasons.length ? reasons[code] : "code " + code);
  }

  private boolean singleLeg() {
    return (capabilities & 32) != 0;
  }

  private void selectNativeInputs() {
    disarm("Internal source selected; verify SB DISARMED before arming");
    nativeLive = true;
    radioActive = true;
    inputId = -1;
    descriptor = "AX12-INTERNAL-260908";
    deviceName = "AX12 internal inputs (no USB cable)";
    axes.clear();
    keys.clear();
    mins.clear();
    maxs.clear();
    centers.clear();
    loadRadio();
    for (int i = 0; i < 7; i++) {
      RadioProfile.Binding b = radio.binding[i];
      if (b.axis < 0) {
        b.axis = NativeRadioInput.AXIS_BASE + i;
        b.low = -500;
        b.high = 500;
        b.center = 0;
        b.captured = 7;
      }
      mins.put(NativeRadioInput.AXIS_BASE + i, -500f);
      maxs.put(NativeRadioInput.AXIS_BASE + i, 500f);
      centers.put(NativeRadioInput.AXIS_BASE + i, 0f);
    }
    nativeNumber = -1;
  }

  private void consumeNativeInputs() {
    if (!nativeLive) return;
    NativeRadioInput.Sample v = nativeRadio.sample();
    if (v == null || !v.fresh(SystemClock.elapsedRealtime())) {
      if (!axes.isEmpty() || armedRequest) {
        axes.clear();
        radioState = new RadioProfile.Snapshot();
        disarm("Internal input heartbeat lost/stale; select SB DISARMED before rearming");
      }
      return;
    }
    if (v.number == nativeNumber) return;
    nativeNumber = v.number;
    for (int i = 0; i < 7; i++)
      axes.put(NativeRadioInput.AXIS_BASE + i, (float) v.values[NativeRadioInput.SOURCES[i]]);
    inputAt = v.at;
    inputEvents++;
    updateRadio();
  }

  private TextView nativeProbeView;
  private CheckBox nativeOwnerCheck;
  private String nativePath = "/dev/ttyS0";
  private NativeUartProbe nativeProbe;
  private final InputTrace inputTrace = new InputTrace();
  private final Handler handler = new Handler(Looper.getMainLooper());
  private final Map<Integer, Float> axes = new TreeMap<>(),
      mins = new TreeMap<>(),
      maxs = new TreeMap<>(),
      centers = new TreeMap<>();
  private final Set<Integer> keys = new HashSet<>();
  private final LinkedHashMap<String, BluetoothDevice> devices = new LinkedHashMap<>();
  private final ArrayDeque<String> logs = new ArrayDeque<>();
  private final ArrayDeque<String> motorFaultHistory = new ArrayDeque<>();
  private android.content.SharedPreferences prefs;
  private LinearLayout body;
  private TextView headline, summary, linkLabel, rawView, calLabel, modeLabel;
  private StickView sticks;
  private BleLink ble;
  private boolean foreground = false,
      armedRequest = false,
      launchPending = false,
      calibrating = false,
      demo = false;
  private int inputId = -1,
      session = 1,
      sequence = 0,
      telemetrySeq = -1,
      robotFlags = 0,
      capabilities = 0,
      mode = 0,
      tab = 0;
  private int forwardAxis = MotionEvent.AXIS_Y,
      turnAxis = MotionEvent.AXIS_X,
      enableKey = KeyEvent.KEYCODE_BUTTON_R1,
      launchKey = KeyEvent.KEYCODE_BUTTON_X;
  private String deviceName = "No input source selected",
      descriptor = "",
      linkStatus = "Not connected",
      lastReason = "Disarmed on startup";
  private boolean invertForward = true, invertTurn = false;
  private float deadzone = .08f, leftDeg = 0, rightDeg = 0, volts = 0;
  private long inputAt = 0,
      telemetryAt = 0,
      armAt = 0,
      lastWrite = 0,
      lastRender = 0,
      lastInputLog = 0;
  private int inputEvents = 0, txCount = 0, rxCount = 0, badFrames = 0, lastFaults = 0;
  private static final int BG = Color.rgb(232, 246, 255),
      PANEL = Color.rgb(255, 255, 255),
      TEXT = Color.rgb(25, 45, 91),
      ACCENT = Color.rgb(20, 145, 122);
  private int enableAxis = -1, cycleAxis = -1;
  private boolean enableAxisLow = false, cycleAxisLow = false, cycleAxisWasHeld = false;

  private boolean radioActive = true;
  private final RadioProfile radio = new RadioProfile();
  private RadioProfile.Snapshot radioState = new RadioProfile.Snapshot();
  private final RadioArming radioArming = new RadioArming();
  private final Set<Integer> seenKeys = new HashSet<>();
  private TextView radioInfo;
  private int learningRole = -1;
  private final Map<Integer, Float> learningAxes = new TreeMap<>();

  private boolean driveEnable() {
    return radioActive ? radioState.sb > 0 : enableHeld();
  }

  private void radioDashboard() {
    LinearLayout layout = row();
    layout.setPadding(0, dp(8), 0, 0);
    mission = new MissionView(this);
    LinearLayout.LayoutParams art = new LinearLayout.LayoutParams(0, dp(300), 1.25f);
    art.setMargins(0, 0, dp(10), 0);
    layout.addView(mission, art);
    LinearLayout status = card();
    status.addView(missionTitle = text("SINGLE LEG / FLIGHT DECK", 13));
    missionStatus = text("STANDBY", 26);
    status.addView(missionStatus);
    missionDetails = text("", 16);
    status.addView(missionDetails);
    Button connect = button("Robot connection  →", () -> showTab(2));
    paintButton(connect, Color.rgb(255, 214, 64), TEXT);
    status.addView(connect);
    modeLabel = text("", 15);
    status.addView(modeLabel);
    layout.addView(status, new LinearLayout.LayoutParams(0, -2, 1));
    body.addView(layout);
    LinearLayout strip = card();
    strip.addView(
        driveHint =
            text(
                "ELEVATOR  Move the leg     •     NEUTRAL  Hold position     •     SB  Disarm /"
                    + " arm",
                16));
    nativeToggle =
        button(
            nativeEnabled ? "Internal inputs ON • turn OFF" : "Internal inputs OFF • turn ON",
            () -> {
              setNativeEnabled(!nativeEnabled);
              showTab(0);
            });
    strip.addView(nativeToggle);
    body.addView(strip);
  }

  private void operatorControls() {
    if (directionTest()) {
      body.addView(text(dualPosition() ? "TWO-LEG POSITION BENCH" : "UNLOADED DIRECTION TEST", 26));
      LinearLayout test = card();
      test.addView(text("ID 1 RIGHT / ID 2 LEFT", 21));
      test.addView(
          text(
              dualPosition()
                  ? "Both motors move together; SC changes require rearming.\n"
                      + "Watchdogs configure automatically; place BOTH outputs at the retracted"
                      + " pose.\n"
                      + "Release then hold Stick B for 2 seconds to capture independent zeros.\n"
                      + "SB arms/disarms both. Elevator extends/retracts; neutral HOLDS.\n"
                      + "Fixed 0–80° / 1 A / 1 rad/s. No jump, gait or spinner.\n"
                      + "No recapture during this boot. Invalid reference requires restart and"
                      + " manual setup.\n"
                      + "Bare-output bench first; no springs."
                  : "SC low: RIGHT only • middle: LEFT only • high: BOTH\n"
                      + "SB: DISARMED / motor enable\n"
                      + "Elevator: choose spin direction; neutral commands zero speed.\n"
                      + "1 A / up to 1 rad/s. Each run stops near 80° measured rotation or 2.5"
                      + " seconds after first motion command. Cycle SB off before another run.\n"
                      + "No zero, homing, calibration or position hold. Stick B is unused. Keep"
                      + " both outputs free of mechanisms and springs.",
              16));
      body.addView(test);
      return;
    }
    body.addView(text("YOUR CONTROLLER", 24));
    body.addView(text("One leg today. The full robot profile stays ready for later.", 15));
    LinearLayout c = card();
    c.addView(text("SB  •  ARM SELECTOR", 18));
    c.addView(
        text(
            "DISARMED → LEG ARMED → FULL ARM selection\n"
                + "The current rig only powers the leg. Spinner and jump outputs are unavailable.",
            15));
    c.addView(text("ELEVATOR  •  EXTEND / RETRACT", 18));
    c.addView(
        text(
            "Neutral holds position. Travel 0–80° at 1 A / 1 rad/s. Set retracted zero using Stick"
                + " B held for 2 seconds while disarmed.",
            15));
    body.addView(c);
    LinearLayout directions = card();
    directions.addView(text("CONTROL DIRECTION", 18));
    for (int role : new int[] {RadioProfile.ELEVATOR, RadioProfile.AILERON, RadioProfile.SB}) {
      final int r = role;
      CheckBox reverse = new CheckBox(this);
      reverse.setText(RadioProfile.LABELS[r] + " • reverse");
      reverse.setTextColor(TEXT);
      reverse.setChecked(radio.binding[r].inverted);
      reverse.setOnCheckedChangeListener(
          (v, on) -> {
            disarm("Control direction changed; select SB DISARMED");
            radio.binding[r].inverted = on;
            saveRadio();
          });
      directions.addView(reverse);
    }
    body.addView(directions);
    LinearLayout future = card();
    future.addView(text("MONITORED FOR THE FUTURE", 18));
    future.addView(
        text(
            "CH3 throttle / spinner • S2 standing height\n"
                + "SC movement style • SD jump switch\n"
                + "These inputs do not trigger those actions on the single-leg firmware.",
            15));
    radioInfo = text("", 14);
    future.addView(radioInfo);
    body.addView(future);
  }

  private void radioControls() {
    body.addView(text("AX12 named channel profile", 20));
    body.addView(
        text(
            deviceName
                + "\n"
                + "RC channel labels are not Android axis IDs. Learn one control at a time, or"
                + " choose its input manually. For CH1/CH2 capture low, centered, high. For CH3"
                + " capture throttle-off and full-throttle endpoints. S2 and switch endpoints can"
                + " use reported Android ranges or captured endpoints.",
            14));
    body.addView(button("Choose input device", this::chooseInput));
    if (nativeLive)
      body.addView(
          text(
              "Internal source mapping is supplied from the verified AX12 capture. Check each"
                  + " control on the dashboard; use Reverse or capture endpoints here if"
                  + " direction/trim needs adjustment. There is no external USB controller"
                  + " requirement.",
              14));
    for (int role = 0; role < 7; role++) {
      final int r = role;
      RadioProfile.Binding b = radio.binding[r];
      body.addView(text(RadioProfile.LABELS[r] + "\n" + bindingDescription(b), 16));
      LinearLayout select = row();
      addButton(select, "Learn axis", () -> startRadioLearn(r));
      addButton(select, "Choose input", () -> chooseRadioSource(r));
      CheckBox inv = new CheckBox(this);
      inv.setText("Reverse");
      inv.setTextColor(TEXT);
      inv.setChecked(b.inverted);
      inv.setOnCheckedChangeListener(
          (v, checked) -> {
            disarm("RC mapping changed");
            b.inverted = checked;
            saveRadio();
          });
      select.addView(
          inv,
          new LinearLayout.LayoutParams(
              (int) (120 * getResources().getDisplayMetrics().density), -2));
      body.addView(select);
      LinearLayout capture = row();
      addButton(
          capture,
          r == RadioProfile.THROTTLE ? "Capture OFF" : "Capture low",
          () -> captureRadio(r, 1));
      if (r == RadioProfile.AILERON || r == RadioProfile.ELEVATOR)
        addButton(capture, "Capture center", () -> captureRadio(r, 2));
      addButton(
          capture,
          r == RadioProfile.THROTTLE ? "Capture full" : "Capture high",
          () -> captureRadio(r, 4));
      body.addView(capture);
    }
    body.addView(
        text(
            "SB low/mid/high = disarmed/drive-only/full. SC low/mid/high = hop/gyro/ambulate."
                + " Reverse a switch if needed. SD's initial state establishes a baseline; both"
                + " later edges request a jump. S2 is a normalized height request, not a measured"
                + " standing height.",
            14));
    radioInfo = text("", 14);
    body.addView(radioInfo);
    rawView = text("", 14);
    body.addView(rawView);
  }

  private String bindingDescription(RadioProfile.Binding b) {
    if (nativeLive
        && b.axis >= NativeRadioInput.AXIS_BASE
        && b.axis < NativeRadioInput.AXIS_BASE + 7)
      return "Internal "
          + RadioProfile.LABELS[b.axis - NativeRadioInput.AXIS_BASE]
          + String.format(
              Locale.US, " • low %.0f / center %.0f / high %.0f", b.low, b.center, b.high);
    if (b.axis >= 0)
      return MotionEvent.axisToString(b.axis)
          + String.format(
              Locale.US,
              " • low %.3f / center %.3f / high %.3f • captured %d/7",
              b.low,
              b.center,
              b.high,
              b.captured);
    if (b.highKey >= 0)
      return b.lowKey >= 0
          ? "Low "
              + KeyEvent.keyCodeToString(b.lowKey)
              + " / middle neither / high "
              + KeyEvent.keyCodeToString(b.highKey)
          : KeyEvent.keyCodeToString(b.highKey) + " • up/down";
    return "UNMAPPED";
  }

  private void loadRadio() {
    radioArming.reset();
    radioState = new RadioProfile.Snapshot();
    seenKeys.clear();
    for (int i = 0; i < 7; i++) {
      RadioProfile.Binding b = radio.binding[i];
      String k = prefix() + "rc." + i + ".";
      b.axis = prefs.getInt(k + "axis", -1);
      b.lowKey = prefs.getInt(k + "lowKey", -1);
      b.highKey = prefs.getInt(k + "highKey", -1);
      b.low = prefs.getFloat(k + "low", -1);
      b.center = prefs.getFloat(k + "center", 0);
      b.high = prefs.getFloat(k + "high", 1);
      b.inverted = prefs.getBoolean(k + "reverse", false);
      b.captured = prefs.getInt(k + "captured", 0);
    }
  }

  private void saveRadio() {
    if (inputId < 0 && !nativeLive) return;
    android.content.SharedPreferences.Editor e = prefs.edit();
    for (int i = 0; i < 7; i++) {
      RadioProfile.Binding b = radio.binding[i];
      String k = prefix() + "rc." + i + ".";
      e.putInt(k + "axis", b.axis)
          .putInt(k + "lowKey", b.lowKey)
          .putInt(k + "highKey", b.highKey)
          .putFloat(k + "low", b.low)
          .putFloat(k + "center", b.center)
          .putFloat(k + "high", b.high)
          .putBoolean(k + "reverse", b.inverted)
          .putInt(k + "captured", b.captured);
    }
    e.apply();
    log("Named RC mapping saved: " + radioMappingText());
  }

  private String radioMappingText() {
    StringBuilder s = new StringBuilder();
    for (int i = 0; i < 7; i++)
      s.append(RadioProfile.LABELS[i])
          .append('=')
          .append(bindingDescription(radio.binding[i]))
          .append(';');
    return s.toString();
  }

  private void startRadioLearn(int role) {
    if (axes.isEmpty()) {
      toast("Move a gimbal first so Android input ranges are known.");
      return;
    }
    disarm("Learning RC axis");
    learningRole = role;
    learningAxes.clear();
    learningAxes.putAll(axes);
    toast(
        "Move ONLY "
            + RadioProfile.LABELS[role]
            + " through a large part of its travel. Use Choose input for key-based switches.");
  }

  private void learnRadioAxis() {
    if (learningRole < 0) return;
    float best = .2f;
    int selected = -1;
    for (int id : axes.keySet()) {
      float span = Math.abs(maxs.getOrDefault(id, 1f) - mins.getOrDefault(id, -1f));
      if (span < .0001f) continue;
      float delta = Math.abs(axes.get(id) - learningAxes.getOrDefault(id, axes.get(id))) / span;
      if (delta > best) {
        best = delta;
        selected = id;
      }
    }
    if (selected >= 0) {
      int r = learningRole;
      learningRole = -1;
      assignRadioAxis(r, selected);
      toast("Learned " + MotionEvent.axisToString(selected));
      showTab(1);
    }
  }

  private void assignRadioAxis(int role, int id) {
    disarm("RC input mapping changed");
    RadioProfile.Binding b = radio.binding[role];
    b.axis = id;
    b.lowKey = b.highKey = -1;
    b.low = mins.getOrDefault(id, -1f);
    b.high = maxs.getOrDefault(id, 1f);
    b.center = (b.low + b.high) / 2;
    b.captured = 0;
    b.inverted = false;
    saveRadio();
    radioState = new RadioProfile.Snapshot();
  }

  private void chooseRadioSource(int role) {
    List<Integer> ids = axisOptions();
    List<String> labels = new ArrayList<>();
    labels.add("Unmapped");
    for (int id : ids) labels.add(MotionEvent.axisToString(id));
    if (role >= RadioProfile.SB)
      labels.add(
          role == RadioProfile.SD
              ? "Use one latching key"
              : "Use two keys (low/high; neither is middle)");
    new AlertDialog.Builder(this)
        .setTitle(RadioProfile.LABELS[role])
        .setItems(
            labels.toArray(new String[0]),
            (d, n) -> {
              if (n == 0) {
                disarm("RC input unmapped");
                radio.binding[role] = new RadioProfile.Binding();
                saveRadio();
                showTab(1);
              } else if (n <= ids.size()) {
                assignRadioAxis(role, ids.get(n - 1));
                showTab(1);
              } else chooseRadioKey(role, -1);
            })
        .show();
  }

  private void chooseRadioKey(int role, int low) {
    TreeSet<Integer> codes = new TreeSet<>(seenKeys);
    for (int k = KeyEvent.KEYCODE_BUTTON_A; k <= KeyEvent.KEYCODE_BUTTON_MODE; k++) codes.add(k);
    for (int k = KeyEvent.KEYCODE_BUTTON_1; k <= KeyEvent.KEYCODE_BUTTON_16; k++) codes.add(k);
    for (int k = KeyEvent.KEYCODE_DPAD_UP; k <= KeyEvent.KEYCODE_DPAD_CENTER; k++) codes.add(k);
    List<Integer> ids = new ArrayList<>(codes);
    String[] names = new String[ids.size()];
    for (int i = 0; i < names.length; i++)
      names[i] = KeyEvent.keyCodeToString(ids.get(i)) + " (" + ids.get(i) + ")";
    new AlertDialog.Builder(this)
        .setTitle(
            role == RadioProfile.SD
                ? "SD latching key"
                : low < 0 ? "Key pressed in LOW position" : "Key pressed in HIGH position")
        .setItems(
            names,
            (d, n) -> {
              int key = ids.get(n);
              if (role != RadioProfile.SD && low < 0) {
                chooseRadioKey(role, key);
                return;
              }
              if (key == low) {
                toast("Low and high must use different keys");
                return;
              }
              disarm("RC key mapping changed");
              RadioProfile.Binding b = new RadioProfile.Binding();
              b.lowKey = low;
              b.highKey = key;
              radio.binding[role] = b;
              saveRadio();
              radioState = new RadioProfile.Snapshot();
              showTab(1);
            })
        .show();
  }

  private void captureRadio(int role, int which) {
    RadioProfile.Binding b = radio.binding[role];
    Float v = axes.get(b.axis);
    if (v == null || !Float.isFinite(v)) {
      toast("Map an axis and move the control first.");
      return;
    }
    disarm("RC calibration changed");
    if (which == 1) b.low = v;
    else if (which == 2) b.center = v;
    else b.high = v;
    b.captured |= which;
    saveRadio();
    showTab(1);
  }

  private String radioArmBlock() {
    if (nativeProbeMode) return "Native probe mode blocks robot outputs";
    if (demo) return "DEMO: no robot output";
    if (!foreground || !hasWindowFocus() || !liveInputFresh())
      return "App focus and live input source required";
    if (!radioState.driveKnown() || !Float.isFinite(radioState.height))
      return "Map and exercise CH1, CH2, SB and SC; check S2 if mapped";
    if (!radio.driveCalibrated() || !radio.distinct())
      return "Capture CH1/CH2 low, center, high; use distinct inputs";
    if (Math.abs(forward()) > .02f || Math.abs(turn()) > .02f)
      return "Center CH1/CH2 before arming";
    if (!freshTelemetry()
        || (robotFlags & readyMask()) != readyMask()
        || (robotFlags & 8) != 0
        || lastFaults != 0) return "Fresh ready, referenced, fault-free robot required";
    if (!benchMode() && (capabilities & (1 << mode)) == 0)
      return "Robot does not implement selected SC movement style";
    return null;
  }

  private void updateRadio() {
    RadioProfile.Snapshot old = radioState;
    radioState = radio.sample(axes, keys, seenKeys, old, deadzone);
    if (radioState.sc >= 0) mode = radioState.sc;
    if (armedRequest
        && (!radioState.driveKnown()
            || radioState.sb == 0
            || !Float.isFinite(radioState.height)
            || radioState.sc != old.sc))
      disarm("SB disarmed, invalid controls, or SC style changed; cycle SB through disarmed");
    boolean conditions = radioArmBlock() == null;
    if (radioState.sb == 2 && (radio.binding[RadioProfile.THROTTLE].captured & 5) != 5)
      conditions = false;
    radioArming.observe(
        radioState, conditions, armedRequest && freshTelemetry() && (robotFlags & 4) != 0);
    if (radioArming.armEdge) requestRadioArm();
    else if (old.sb != radioState.sb && radioState.sb > 0 && !armedRequest) {
      String why = radioArmBlock();
      lastReason =
          why != null
              ? why
              : radioState.sb == 2 && (!radioArming.spinnerLowQualified)
                  ? "Full arm blocked: calibrate CH3, set throttle low, then cycle SB through"
                      + " disarmed"
                  : "Cycle SB through disarmed before arming";
    }
    if (radioArming.jumpEdge) requestRadioJump();
    if (old.sb != radioState.sb || old.sc != radioState.sc || radioArming.jumpEdge) sendFrame();
  }

  private void requestRadioArm() {
    String why = radioArmBlock();
    if (why == null && !radioArming.armEdge)
      why = "Cycle SB through disarmed, then select drive/full arm";
    if (why != null) {
      lastReason = why;
      log("RC arm blocked: " + why);
      return;
    }
    armedRequest = true;
    armAt = SystemClock.elapsedRealtime();
    lastReason = "SB arm request; waiting for robot confirmation";
    log(lastReason);
  }

  private void requestRadioJump() {
    if (nativeProbeMode) return;
    if (!radioArming.jumpEdge
        || !armedRequest
        || radioState.sb <= 0
        || !freshTelemetry()
        || (robotFlags & 4) == 0
        || (capabilities & 16) == 0) {
      log(
          "SD edge ignored: confirmed drive arm and robot full-jump capability required; no request"
              + " queued");
      return;
    }
    if (launchPending) {
      log("SD edge ignored: previous jump request still pending");
      return;
    }
    launchPending = true;
    log("SD FULL-POWER JUMP request • " + RadioProfile.STYLE_NAMES[mode]);
  }

  private void renderRadio() {
    String sb = radioState.sb < 0 ? "UNMAPPED / UNKNOWN" : RadioProfile.ARM_NAMES[radioState.sb],
        sc = radioState.sc < 0 ? "UNMAPPED / UNKNOWN" : RadioProfile.STYLE_NAMES[radioState.sc];
    if (modeLabel != null)
      modeLabel.setText(
          "SB: "
              + sb
              + "   SC: "
              + sc
              + "\nDrive: "
              + (armedRequest && freshTelemetry() && (robotFlags & 4) != 0
                  ? "confirmed armed"
                  : "disarmed / unconfirmed")
              + "   Spinner: "
              + (freshTelemetry() && (robotFlags & 16) != 0
                  ? "robot reports armed"
                  : "disabled / unconfirmed"));
    float[] mix = RadioProfile.mixPreview(forward(), turn());
    String description =
        String.format(
            Locale.US,
            "CH3 throttle: %s   S2 height: %s\n"
                + "CH1 steering: %s   CH2 forward: %s\n"
                + "SD: %s • either edge requests jump\n"
                + "Normalized mix preview L/R: %s / %s (not motor targets)",
            percent(radioState.throttle),
            radio.binding[RadioProfile.HEIGHT].axis < 0
                ? "unmapped (50% default)"
                : percent(radioState.height),
            signedDisplay(turn()),
            signedDisplay(forward()),
            radioState.sd < 0 ? "unknown" : Integer.toString(radioState.sd),
            signedDisplay(mix[0]),
            signedDisplay(mix[1]));
    if (radioInfo != null) radioInfo.setText(description);
    if (rawView != null) rawView.append("\n" + description);
  }

  private String signedDisplay(float v) {
    return Float.isFinite(v) ? String.format(Locale.US, "%+.3f", v) : "unknown";
  }

  private String percent(float v) {
    return Float.isFinite(v) ? String.format(Locale.US, "%.0f%%", v * 100) : "unknown";
  }

  private boolean axisHeld(int id, boolean low) {
    if (!axes.containsKey(id)) return false;
    float v = axes.get(id), mid = (mins.getOrDefault(id, -1f) + maxs.getOrDefault(id, 1f)) / 2;
    return low ? v < mid : v > mid;
  }

  private boolean enableHeld() {
    return enableAxis >= 0 ? axisHeld(enableAxis, enableAxisLow) : keys.contains(enableKey);
  }

  private String prefix() {
    return "device." + descriptor + ".";
  }

  @Override
  public void onCreate(Bundle b) {
    super.onCreate(b);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    prefs = getSharedPreferences("ax12", MODE_PRIVATE);
    nativeProbeMode = prefs.getBoolean("nativeProbeMode", false);
    nativeRadio = new NativeRadioInput(new NativeRadioInput.LivePort());
    nativeEnabled = prefs.getBoolean("nativeEnabled", true);
    session = 1 + new java.security.SecureRandom().nextInt(65535);
    ble =
        new BleLink(
            this,
            new BleLink.Listener() {
              public void status(String s) {
                linkStatus = s;
                log(s);
                render();
              }

              public void found(BluetoothDevice d) {
                try {
                  devices.put(d.getAddress(), d);
                  showRobotCard(d);
                } catch (SecurityException ignored) {
                }
              }

              public void lost() {
                linkStatus = "Disconnected; reconnect manually";
                telemetryAt = 0;
                telemetrySeq = -1;
                inputStopSent = false;
                robotFlags = capabilities = 0;
                disarm("Bluetooth session ended");
              }

              public void telemetry(byte[] b) {
                acceptTelemetry(b);
              }
            });
    ((InputManager) getSystemService(INPUT_SERVICE)).registerInputDeviceListener(this, handler);
    nativeProbe =
        new NativeUartProbe(
            new NativeUartProbe.DevicePort(),
            () ->
                handler.post(
                    () -> {
                      if (!isDestroyed()) render();
                    }));
    if (nativeEnabled) {
      nativeProbeMode = false;
      prefs.edit().putBoolean("nativeProbeMode", false).apply();
      selectNativeInputs();
    } else detectInput();
    showTab(0);
    log("AX12 app 0.7.1. Named RC profile; robot-side spinner/jump outputs not implemented.");
    handler.post(pump);
  }

  private void log(String s) {
    String text = String.format(Locale.US, "%d %s", SystemClock.elapsedRealtime(), s);
    logs.addLast(text);
    while (logs.size() > 2000) logs.removeFirst();
  }

  private TextView text(String s, int size) {
    TextView t = new TextView(this);
    t.setText(s);
    t.setTextColor(TEXT);
    t.setTextSize(size);
    t.setPadding(dp(6), dp(5), dp(6), dp(5));
    t.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
    return t;
  }

  private Button button(String s, Runnable action) {
    Button b = new Button(this);
    b.setText(s);
    b.setAllCaps(false);
    b.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
    b.setTextSize(14);
    b.setMinHeight(dp(44));
    paintButton(b, TEXT, Color.WHITE);
    b.setOnClickListener(v -> action.run());
    return b;
  }

  private LinearLayout row() {
    LinearLayout l = new LinearLayout(this);
    l.setOrientation(LinearLayout.HORIZONTAL);
    return l;
  }

  private void addButton(LinearLayout l, String title, Runnable r) {
    l.addView(button(title, r), new LinearLayout.LayoutParams(0, -2, 1));
  }

  private void showTab(int which) {
    tab = which;
    learningRole = -1;
    radioInfo = null;
    nativeProbeView = null;
    nativeOwnerCheck = null;
    nativeToggle = null;
    nativeLiveStatus = null;
    inputMonitor = null;
    inputMonitorStatus = rawSourcesView = null;
    mission = null;
    missionStatus =
        missionDetails = connectionSteps = connectionHelp = missionTitle = driveHint = null;
    robotCards = null;
    disconnectButton = null;
    robotButtons.clear();
    shownRobots.clear();
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setFitsSystemWindows(true);
    root.setBackgroundColor(BG);
    root.setPadding(12, 8, 12, 8);
    LinearLayout top = row();
    headline = text("JUMPING FLASH  •  AX12", 20);
    top.addView(headline, new LinearLayout.LayoutParams(0, -2, 1));
    Button stop = button("■  STOP", this::operatorStop);
    paintButton(stop, Color.rgb(215, 53, 73), Color.WHITE);
    top.addView(stop);
    root.addView(top);
    LinearLayout nav = row();
    String[] tabs = {"01  FLIGHT DECK", "02  CONNECT", "03  CONTROLS", "04  TOOLS"};
    int[] pages = {0, 2, 1, 3};
    for (int i = 0; i < tabs.length; i++) {
      final int page = pages[i];
      Button b = button(tabs[i], () -> showTab(page));
      paintButton(b, page == which ? TEXT : Color.WHITE, page == which ? Color.WHITE : TEXT);
      LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(44), 1);
      lp.setMargins(dp(3), dp(5), dp(3), dp(8));
      nav.addView(b, lp);
    }
    root.addView(nav);
    ScrollView scroll = new ScrollView(this);
    body = new LinearLayout(this);
    body.setOrientation(LinearLayout.VERTICAL);
    scroll.addView(body);
    root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    setContentView(root);
    summary = linkLabel = rawView = calLabel = modeLabel = null;
    sticks = null;
    if (which == 0) dashboard();
    if (which == 1) operatorControls();
    if (which == 2) connections();
    if (which == 3) diagnostics();
    if (which == 4) nativeInputPage();
    render();
  }

  private void dashboard() {
    if (radioActive) {
      radioDashboard();
      return;
    }
    summary = text("", 18);
    body.addView(summary);
    sticks = new StickView();
    body.addView(sticks, new LinearLayout.LayoutParams(-1, 200));
    LinearLayout actions = row();
    addButton(actions, "Arm request", this::requestArm);
    addButton(
        actions,
        "Disarm",
        () -> {
          disarm("Operator disarm");
          sendFrame();
        });
    addButton(actions, "One-shot cycle", this::requestLaunch);
    body.addView(actions);
    modeLabel = text("", 16);
    body.addView(modeLabel);
    LinearLayout modes = row();
    String[] names = {"Manual", "Jump", "Gyro walk"};
    for (int i = 0; i < 3; i++) {
      final int n = i;
      addButton(
          modes,
          names[i],
          () -> {
            disarm("Mode changed");
            mode = n;
            render();
          });
    }
    body.addView(modes);
    body.addView(
        text(
            "Hold mapped enable, center gimbals, then tap Arm request. Reconnection never rearms."
                + " Jump/gyro requests require matching robot capabilities. Spinner control is not"
                + " implemented.",
            14));
    body.addView(
        button(
            demo ? "Exit DEMO" : "Enter DEMO (no Bluetooth commands)",
            () -> {
              disarm("Demo mode changed");
              ble.close();
              demo = !demo;
              telemetryAt = 0;
              linkStatus = demo ? "DEMO — local controls only" : "Not connected";
              log(linkStatus);
              showTab(0);
            }));
  }

  private void controls() {
    if (radioActive) {
      radioControls();
      return;
    }
    body.addView(text("Game controller mapping • saved per device", 20));
    body.addView(
        text(
            deviceName
                + "\n"
                + "Move either gimbal to select the controller. Calibration: center → sweep to all"
                + " endpoints → save. Motors remain disarmed.",
            14));
    LinearLayout a = row();
    addButton(a, "Choose input device", this::chooseInput);
    addButton(a, "Center / start calibration", this::startCalibration);
    addButton(a, "Save endpoints", this::saveCalibration);
    body.addView(a);
    axisSelector("Forward / extension", true);
    axisSelector("Turn / second axis", false);
    keySelector("Hold-to-enable", true);
    keySelector("One-shot cycle", false);
    switchAxisSelector("Enable switch axis (optional)", true);
    switchAxisSelector("Cycle switch axis (optional)", false);
    LinearLayout inv = row();
    CheckBox f = new CheckBox(this);
    f.setText("Invert forward");
    f.setTextColor(TEXT);
    f.setChecked(invertForward);
    f.setOnCheckedChangeListener(
        (v, c) -> {
          disarm("Mapping changed");
          invertForward = c;
          saveMapping();
        });
    inv.addView(f);
    CheckBox t = new CheckBox(this);
    t.setText("Invert turn");
    t.setTextColor(TEXT);
    t.setChecked(invertTurn);
    t.setOnCheckedChangeListener(
        (v, c) -> {
          disarm("Mapping changed");
          invertTurn = c;
          saveMapping();
        });
    inv.addView(t);
    body.addView(inv);
    body.addView(text("Deadzone", 14));
    SeekBar dz = new SeekBar(this);
    dz.setMax(25);
    dz.setProgress(Math.round(deadzone * 100));
    dz.setOnSeekBarChangeListener(
        new SeekBar.OnSeekBarChangeListener() {
          public void onProgressChanged(SeekBar s, int p, boolean user) {
            if (user) {
              deadzone = p / 100.f;
              disarm("Deadzone changed");
              saveMapping();
            }
          }

          public void onStartTrackingTouch(SeekBar s) {}

          public void onStopTrackingTouch(SeekBar s) {}
        });
    body.addView(dz);
    calLabel = text("", 14);
    rawView = text("", 14);
    body.addView(calLabel);
    body.addView(rawView);
  }

  private List<Integer> axisOptions() {
    TreeSet<Integer> options = new TreeSet<>();
    for (int x :
        new int[] {
          MotionEvent.AXIS_X,
          MotionEvent.AXIS_Y,
          MotionEvent.AXIS_Z,
          MotionEvent.AXIS_RZ,
          MotionEvent.AXIS_RX,
          MotionEvent.AXIS_RY,
          MotionEvent.AXIS_HAT_X,
          MotionEvent.AXIS_HAT_Y,
          MotionEvent.AXIS_LTRIGGER,
          MotionEvent.AXIS_RTRIGGER
        }) options.add(x);
    InputDevice d = InputDevice.getDevice(inputId);
    if (d != null) for (InputDevice.MotionRange r : d.getMotionRanges()) options.add(r.getAxis());
    return new ArrayList<>(options);
  }

  private void axisSelector(String label, boolean forward) {
    body.addView(text(label, 14));
    List<Integer> ids = axisOptions();
    String[] names = new String[ids.size()];
    for (int i = 0; i < ids.size(); i++) names[i] = MotionEvent.axisToString(ids.get(i));
    Spinner s = new Spinner(this);
    s.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, names));
    s.setSelection(Math.max(0, ids.indexOf(forward ? forwardAxis : turnAxis)));
    s.setOnItemSelectedListener(
        new AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(AdapterView<?> v) {}

          public void onItemSelected(AdapterView<?> v, View x, int p, long id) {
            int chosen = ids.get(p);
            if (chosen != (forward ? forwardAxis : turnAxis)) {
              disarm("Axis mapping changed");
              if (forward) forwardAxis = chosen;
              else turnAxis = chosen;
              prefs.edit().putBoolean(prefix() + "calibrated", false).apply();
              saveMapping();
            }
          }
        });
    body.addView(s);
  }

  private void keySelector(String label, boolean enable) {
    body.addView(text(label, 14));
    int[] ids = {
      KeyEvent.KEYCODE_BUTTON_R1,
      KeyEvent.KEYCODE_BUTTON_L1,
      KeyEvent.KEYCODE_BUTTON_R2,
      KeyEvent.KEYCODE_BUTTON_L2,
      KeyEvent.KEYCODE_BUTTON_A,
      KeyEvent.KEYCODE_BUTTON_B,
      KeyEvent.KEYCODE_BUTTON_X,
      KeyEvent.KEYCODE_BUTTON_Y,
      KeyEvent.KEYCODE_BUTTON_START,
      KeyEvent.KEYCODE_BUTTON_SELECT,
      KeyEvent.KEYCODE_BUTTON_THUMBL,
      KeyEvent.KEYCODE_BUTTON_THUMBR,
      KeyEvent.KEYCODE_DPAD_UP,
      KeyEvent.KEYCODE_DPAD_DOWN,
      KeyEvent.KEYCODE_DPAD_LEFT,
      KeyEvent.KEYCODE_DPAD_RIGHT,
      KeyEvent.KEYCODE_BUTTON_1,
      KeyEvent.KEYCODE_BUTTON_2,
      KeyEvent.KEYCODE_BUTTON_3,
      KeyEvent.KEYCODE_BUTTON_4,
      KeyEvent.KEYCODE_BUTTON_5,
      KeyEvent.KEYCODE_BUTTON_6,
      KeyEvent.KEYCODE_BUTTON_7,
      KeyEvent.KEYCODE_BUTTON_8,
      KeyEvent.KEYCODE_BUTTON_9,
      KeyEvent.KEYCODE_BUTTON_10,
      KeyEvent.KEYCODE_BUTTON_11,
      KeyEvent.KEYCODE_BUTTON_12,
      KeyEvent.KEYCODE_BUTTON_13,
      KeyEvent.KEYCODE_BUTTON_14,
      KeyEvent.KEYCODE_BUTTON_15,
      KeyEvent.KEYCODE_BUTTON_16
    };
    String[] names = new String[ids.length];
    int selection = 0;
    for (int i = 0; i < ids.length; i++) {
      names[i] = KeyEvent.keyCodeToString(ids[i]);
      if (ids[i] == (enable ? enableKey : launchKey)) selection = i;
    }
    Spinner s = new Spinner(this);
    s.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, names));
    s.setSelection(selection);
    s.setOnItemSelectedListener(
        new AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(AdapterView<?> v) {}

          public void onItemSelected(AdapterView<?> v, View x, int p, long id) {
            if (ids[p] != (enable ? enableKey : launchKey)) {
              disarm("Button mapping changed");
              if (enable) enableKey = ids[p];
              else launchKey = ids[p];
              saveMapping();
            }
          }
        });
    body.addView(s);
  }

  private void switchAxisSelector(String label, boolean enable) {
    body.addView(text(label, 14));
    List<Integer> ids = axisOptions();
    ids.add(0, -1);
    String[] names = new String[ids.size()];
    for (int i = 0; i < ids.size(); i++)
      names[i] = ids.get(i) < 0 ? "Use mapped button" : MotionEvent.axisToString(ids.get(i));
    Spinner spinner = new Spinner(this);
    spinner.setAdapter(
        new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item, names));
    spinner.setSelection(Math.max(0, ids.indexOf(enable ? enableAxis : cycleAxis)));
    spinner.setOnItemSelectedListener(
        new AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(AdapterView<?> v) {}

          public void onItemSelected(AdapterView<?> v, View x, int p, long id) {
            if (ids.get(p) != (enable ? enableAxis : cycleAxis)) {
              disarm("Switch mapping changed");
              if (enable) enableAxis = ids.get(p);
              else cycleAxis = ids.get(p);
              cycleAxisWasHeld = true;
              saveMapping();
            }
          }
        });
    body.addView(spinner);
    CheckBox low = new CheckBox(this);
    low.setText("Active at low end of axis");
    low.setTextColor(TEXT);
    low.setChecked(enable ? enableAxisLow : cycleAxisLow);
    low.setOnCheckedChangeListener(
        (v, checked) -> {
          disarm("Switch direction changed");
          if (enable) enableAxisLow = checked;
          else cycleAxisLow = checked;
          cycleAxisWasHeld = true;
          saveMapping();
        });
    body.addView(low);
  }

  private void connections() {
    body.addView(text("LET’S LINK UP", 28));
    body.addView(text("Power on the Stick with the matching Jumping Flash firmware.", 16));
    LinearLayout c = card();
    connectionSteps = text("01 FIND     →     02 PAIR     →     03 CONNECTED", 18);
    c.addView(connectionSteps);
    linkLabel = text("", 16);
    c.addView(linkLabel);
    LinearLayout actions = row();
    Button find = button("Find my Stick", this::findRobot);
    paintButton(find, Color.rgb(255, 214, 64), TEXT);
    actions.addView(find, new LinearLayout.LayoutParams(0, -2, 1));
    if (!prefs.getString("lastRobot", "").isEmpty())
      addButton(actions, "Reconnect last Stick", this::reconnectRobot);
    disconnectButton =
        button(
            "Disconnect",
            () -> {
              disarm("Operator disconnected");
              sendFrame();
              ble.close();
            });
    actions.addView(disconnectButton, new LinearLayout.LayoutParams(0, -2, 1));
    c.addView(actions);
    body.addView(c);
    body.addView(text("YOUR ROBOT", 16));
    robotCards = new LinearLayout(this);
    robotCards.setOrientation(LinearLayout.VERTICAL);
    body.addView(robotCards);
    for (BluetoothDevice d : devices.values()) showRobotCard(d);
    LinearLayout help = card();
    help.addView(text("WHAT HAPPENS NEXT", 17));
    help.addView(
        connectionHelp =
            text(
                "Tap your robot when it appears. If Android asks to pair, accept; we’ll finish"
                    + " connecting automatically. Connecting never arms the leg.\n"
                    + "Once linked: inputs ON → SB DISARMED → set retracted zero with Stick B →"
                    + " select LEG ARMED.",
                15));
    body.addView(help);
    body.addView(
        button(
            "Bluetooth settings",
            () -> startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS))));
  }

  private void operatorStop() {
    if (nativeProbe != null) nativeProbe.stop();
    disarm("Operator STOP");
    maintenanceCancelPending = directionTest();
    sendFrame();
  }

  private void enterNativeProbeMode() {
    setNativeEnabled(false);
    disarm("Entering native probe mode");
    sendFrame();
    ble.close();
    nativeProbeMode = true;
    prefs.edit().putBoolean("nativeProbeMode", true).apply();
    render();
  }

  private void restoreRadioWhenClosed(int attempt) {
    if (!foreground) return;
    if (nativeProbe.running() || nativeRadio.running()) {
      if (attempt >= 25) {
        toast("Reader still closing; retry Restore after it stops");
        return;
      }
      handler.postDelayed(() -> restoreRadioWhenClosed(attempt + 1), 50);
      return;
    }
    Intent launch = getPackageManager().getLaunchIntentForPackage("com.Flyshark.RadioMasterAX");
    if (launch == null) {
      toast("Open RadioMasterAX from the Android launcher");
      return;
    }
    startActivity(launch);
  }

  private void nativeInputPage() {
    body.addView(text("AX12 internal radio • advanced capture", 20));
    body.addView(
        text(
            "Keep RadioMasterAX stopped while this app owns the internal UART. No DSC-to-USB cable"
                + " is needed. Live input verifies its own serial settings and can send up to three"
                + " fixed link-discovery probes before the first valid input. Cold-start"
                + " independence is still being tested. Inputs resume after backgrounding. Turn"
                + " them OFF before opening RadioMasterAX. Verify gimbals centered, throttle OFF"
                + " and SB DISARMED on the dashboard before connecting the rig.",
            14));
    body.addView(
        button(
            "RadioMasterAX app settings",
            () -> {
              setNativeEnabled(false);
              operatorStop();
              startActivity(
                  new Intent(
                      android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                      android.net.Uri.parse("package:com.Flyshark.RadioMasterAX")));
            }));
    body.addView(
        nativeToggle =
            button(
                nativeEnabled ? "Internal inputs ON • turn OFF" : "Internal inputs OFF • turn ON",
                () -> {
                  setNativeEnabled(!nativeEnabled);
                  showTab(4);
                }));
    body.addView(
        button(
            "Retry internal input connection",
            () -> {
              setNativeEnabled(true);
              showTab(0);
            }));
    body.addView(nativeLiveStatus = text(nativeRadio.status(), 14));
    body.addView(text("Optional passive capture (blocks robot outputs)", 18));
    body.addView(
        text(
            "This probe cannot control the robot. It closes Bluetooth and keeps outputs blocked"
                + " until you explicitly exit probe mode. Remove the external DSC-to-USB cable for"
                + " the test. No root, baud changes, UART writes or transmitter configuration"
                + " commands are used.",
            14));
    LinearLayout setup = row();
    addButton(setup, "1. Enter probe mode", this::enterNativeProbeMode);
    addButton(
        setup,
        "2. Stock app settings",
        () -> {
          enterNativeProbeMode();
          nativeProbe.stop();
          startActivity(
              new Intent(
                  android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                  android.net.Uri.parse("package:com.Flyshark.RadioMasterAX")));
        });
    body.addView(setup);
    body.addView(
        text(
            "In stock app settings, use Force stop for this test, then return here. Stopping"
                + " the stock app is required: locking this port will not disconnect readers"
                + " already using it. Native RC operation may be interrupted; keep the"
                + " robot disarmed. Restore the stock app after capture.",
            14));
    CheckBox owner = new CheckBox(this);
    nativeOwnerCheck = owner;
    owner.setText(
        "I have stopped the stock RadioMaster app and other serial readers for this test");
    owner.setTextColor(TEXT);
    body.addView(owner);
    Spinner ports = new Spinner(this);
    ports.setAdapter(
        new ArrayAdapter<>(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            new String[] {
              "/dev/ttyS0 (default internal port)", "/dev/ttyS4 (alternate listed in stock app)"
            }));
    ports.setSelection(nativePath.endsWith("4") ? 1 : 0);
    ports.setOnItemSelectedListener(
        new AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(AdapterView<?> p) {}

          public void onItemSelected(AdapterView<?> p, View v, int i, long id) {
            nativePath = i == 0 ? "/dev/ttyS0" : "/dev/ttyS4";
          }
        });
    body.addView(ports);
    LinearLayout actions = row();
    addButton(
        actions,
        "3. Start 45s passive capture",
        () -> {
          if (nativeRadio.running()) {
            toast("Stop live reader and wait for close first");
            return;
          }
          if (!nativeProbeMode) {
            toast("Enter probe mode first");
            return;
          }
          if (!owner.isChecked()) {
            toast("Stop the stock app and confirm ownership first");
            return;
          }
          nativeProbe.start(nativePath, true);
        });
    addButton(actions, "Stop capture", () -> nativeProbe.stop());
    body.addView(actions);
    body.addView(
        button(
            "Refresh port access information",
            () -> {
              log(nativeProbe.metadata());
              toast(nativeProbe.metadata());
            }));
    body.addView(text(nativeProbe.metadata(), 14));
    nativeProbeView = text("", 14);
    body.addView(nativeProbeView);
    body.addView(
        text(
            "Raw sources shown below are Sept-build layout candidates, not verified robot inputs."
                + " Export the capture via Diagnostics. Access denied, busy port, no bytes, bad"
                + " frames and changed layouts are separate outcomes; none enables outputs.",
            14));
    LinearLayout finish = row();
    addButton(
        finish,
        "Restore stock RadioMaster app",
        () -> {
          nativeProbe.stop();
          setNativeEnabled(false);
          disarm("Restoring stock app");
          ble.close();
          restoreRadioWhenClosed(0);
        });
    addButton(
        finish,
        "Exit probe mode",
        () -> {
          if (nativeProbe.running() || nativeRadio.running()) {
            toast("Stop capture and wait for port close first");
            return;
          }
          nativeProbeMode = false;
          prefs.edit().putBoolean("nativeProbeMode", false).apply();
          radioState = new RadioProfile.Snapshot();
          axes.clear();
          keys.clear();
          seenKeys.clear();
          disarm("Probe ended; fresh input and deliberate rearm required");
          showTab(0);
        });
    body.addView(finish);
  }

  private void diagnostics() {
    body.addView(text("PIT TOOLS", 26));
    body.addView(button("Live input monitor • gimbals, dials & switches", this::inputMonitorPage));
    if (dualPosition())
      body.addView(
          text(
              "Motor watchdogs configure automatically in the background. No routine watchdog"
                  + " action required.",
              16));
    if (directionTest() && !dualPosition()) {
      LinearLayout checks = card();
      checks.addView(
          text(dualPosition() ? "TWO-LEG POSITION BENCH" : "TWO-MOTOR DIRECTION TEST", 20));
      checks.addView(
          text(
              "After powering both motors: keep SB DISARMED and center gimbals. This check briefly"
                  + " enables each motor at ZERO speed and 1 A to verify its CAN watchdog. Keep"
                  + " outputs unloaded. No mechanical zero is set.",
              15));
      checks.addView(button("Check BOTH motor watchdogs • zero speed", this::requestWatchdogCheck));
      body.addView(checks);
    }
    body.addView(text("Setup and troubleshooting live here. Nothing here arms the robot.", 15));
    LinearLayout c = card();
    c.addView(text("INTERNAL RADIO INPUT", 18));
    nativeToggle =
        button(
            nativeEnabled ? "Internal inputs ON • turn OFF" : "Internal inputs OFF • turn ON",
            () -> {
              setNativeEnabled(!nativeEnabled);
              showTab(3);
            });
    c.addView(nativeToggle);
    nativeLiveStatus = text(nativeRadio.status(), 14);
    c.addView(nativeLiveStatus);
    c.addView(
        button(
            "Retry input connection",
            () -> {
              setNativeEnabled(true);
              showTab(3);
            }));
    body.addView(c);
    body.addView(
        button(
            "Export session log",
            () -> {
              Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
              i.setType("text/plain");
              i.putExtra(Intent.EXTRA_TITLE, "jumping-flash-ax12-log.txt");
              i.addCategory(Intent.CATEGORY_OPENABLE);
              startActivityForResult(i, 30);
            }));
    body.addView(
        button(
            "Detailed input calibration",
            () -> {
              showTab(1);
              body.removeAllViews();
              radioControls();
              render();
            }));
    body.addView(
        button(
            "Live diagnostics and logs",
            () -> {
              showTab(3);
              body.removeAllViews();
              legacyDiagnostics();
              render();
            }));
    body.addView(button("Advanced serial probe / stock app setup", () -> showTab(4)));
  }

  private void legacyDiagnostics() {
    body.addView(
        text(
            "Dial probe: tap Reset input trace, turn ONLY S1/S2 through their travel, then export"
                + " the log. This captures generic motion and key events from all devices/sources,"
                + " including rotary/scroll events. It does not assign those events to robot"
                + " controls.",
            14));
    body.addView(
        button(
            "Reset input trace / probe dials",
            () -> {
              inputTrace.clear();
              log("All-source dial probe reset");
            }));
    rawView = text("", 14);
    body.addView(rawView);
    body.addView(
        button(
            "Export diagnostic log",
            () -> {
              Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
              i.setType("text/plain");
              i.putExtra(Intent.EXTRA_TITLE, "jumping-flash-ax12-log.txt");
              i.addCategory(Intent.CATEGORY_OPENABLE);
              startActivityForResult(i, 30);
            }));
    body.addView(
        button(
            "Clear log",
            () -> {
              logs.clear();
              log("Log cleared by operator");
            }));
    body.addView(
        text(
            "Input age measures the last change event. Android may not emit repeated events for"
                + " stationary gimbals. BLE telemetry must remain fresh within200ms; a missing"
                + " write callback for150ms closes the link. Foreground/focus/device loss disarms."
                + " Robot firmware must independently enforce its own command deadline and motor"
                + " watchdog.",
            14));
  }

  private void chooseInput() {
    List<InputDevice> available = new ArrayList<>();
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (isController(d)) available.add(d);
    }
    if (available.isEmpty()) {
      toast("No Android game controller devices found. Move a gimbal and retry.");
      return;
    }
    String[] names = new String[available.size()];
    for (int i = 0; i < names.length; i++) names[i] = available.get(i).getName();
    new AlertDialog.Builder(this)
        .setTitle("Game controller")
        .setItems(
            names,
            (d, n) -> {
              selectInput(available.get(n));
              showTab(1);
            })
        .show();
  }

  private void chooseBle() {
    if (nativeProbeMode) {
      toast("Exit native probe mode before Bluetooth connection");
      return;
    }
    if (!ble.permissions()) return;
    List<BluetoothDevice> found = new ArrayList<>(devices.values());
    if (found.isEmpty()) {
      toast("Scan first, then choose a device.");
      return;
    }
    String[] names = new String[found.size()];
    for (int i = 0; i < names.length; i++) {
      BluetoothDevice d = found.get(i);
      try {
        names[i] =
            (d.getName() == null ? "Unnamed BLE device" : d.getName()) + "\n" + d.getAddress();
      } catch (SecurityException denied) {
        toast("Bluetooth permission was revoked. Grant it and scan again.");
        return;
      }
    }
    new AlertDialog.Builder(this)
        .setTitle("Discovered BLE devices")
        .setItems(
            names,
            (d, n) -> {
              disarm("New Bluetooth connection");
              telemetryAt = 0;
              telemetrySeq = -1;
              session = 1 + new java.security.SecureRandom().nextInt(65535);
              ble.connect(found.get(n));
            })
        .show();
  }

  private boolean isController(InputDevice d) {
    return d != null
        && ((d.getSources() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
            || (d.getSources() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD);
  }

  private void detectInput() {
    if (nativeLive) return;
    if (InputDevice.getDevice(inputId) != null) return;
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (isController(d)) {
        selectInput(d);
        break;
      }
    }
  }

  private void selectInput(InputDevice d) {
    if (nativeRadio != null) setNativeEnabled(false);
    nativeLive = false;
    if (d.getId() == inputId) return;
    disarm("Input device selected");
    inputId = d.getId();
    deviceName = d.getName();
    descriptor = d.getDescriptor();
    axes.clear();
    keys.clear();
    mins.clear();
    maxs.clear();
    centers.clear();
    calibrating = false;
    inputAt = 0;
    forwardAxis = prefs.getInt(prefix() + "forward", MotionEvent.AXIS_Y);
    turnAxis = prefs.getInt(prefix() + "turn", MotionEvent.AXIS_X);
    enableKey = prefs.getInt(prefix() + "enable", KeyEvent.KEYCODE_BUTTON_R1);
    launchKey = prefs.getInt(prefix() + "launch", KeyEvent.KEYCODE_BUTTON_X);
    invertForward = prefs.getBoolean(prefix() + "invertF", true);
    invertTurn = prefs.getBoolean(prefix() + "invertT", false);
    deadzone = prefs.getFloat(prefix() + "deadzone", .08f);
    enableAxis = prefs.getInt(prefix() + "enableAxis", -1);
    cycleAxis = prefs.getInt(prefix() + "cycleAxis", -1);
    enableAxisLow = prefs.getBoolean(prefix() + "enableAxisLow", false);
    cycleAxisLow = prefs.getBoolean(prefix() + "cycleAxisLow", false);
    cycleAxisWasHeld = true;
    for (InputDevice.MotionRange r : d.getMotionRanges()) {
      int id = r.getAxis();
      mins.put(id, prefs.getFloat(prefix() + id + "min", r.getMin()));
      maxs.put(id, prefs.getFloat(prefix() + id + "max", r.getMax()));
      centers.put(id, prefs.getFloat(prefix() + id + "center", (r.getMin() + r.getMax()) / 2));
    }
    loadRadio();
    log("Input: " + deviceName + " descriptor=" + descriptor + " sources=" + d.getSources());
  }

  private void saveMapping() {
    if (inputId < 0 && !nativeLive) return;
    prefs
        .edit()
        .putInt(prefix() + "enableAxis", enableAxis)
        .putInt(prefix() + "cycleAxis", cycleAxis)
        .putBoolean(prefix() + "enableAxisLow", enableAxisLow)
        .putBoolean(prefix() + "cycleAxisLow", cycleAxisLow)
        .putInt(prefix() + "forward", forwardAxis)
        .putInt(prefix() + "turn", turnAxis)
        .putInt(prefix() + "enable", enableKey)
        .putInt(prefix() + "launch", launchKey)
        .putBoolean(prefix() + "invertF", invertForward)
        .putBoolean(prefix() + "invertT", invertTurn)
        .putFloat(prefix() + "deadzone", deadzone)
        .apply();
  }

  private void startCalibration() {
    if (inputAt == 0) {
      toast("Move the gimbals first, then center them.");
      return;
    }
    disarm("Calibration started");
    for (int id : axes.keySet()) {
      float v = axes.get(id);
      centers.put(id, v);
      mins.put(id, v);
      maxs.put(id, v);
    }
    calibrating = true;
    log("Calibration center captured; sweep both gimbals through full range.");
    render();
  }

  private void saveCalibration() {
    if (!calibrating) {
      toast("Start calibration first.");
      return;
    }
    for (int id : new int[] {forwardAxis, turnAxis}) {
      float c = centers.getOrDefault(id, 0f);
      if (c - mins.getOrDefault(id, c) < .1f || maxs.getOrDefault(id, c) - c < .1f) {
        toast("Sweep both mapped axes to both endpoints before saving.");
        return;
      }
    }
    android.content.SharedPreferences.Editor e = prefs.edit();
    for (int id : centers.keySet()) {
      e.putFloat(prefix() + id + "center", centers.get(id));
      e.putFloat(prefix() + id + "min", mins.get(id));
      e.putFloat(prefix() + id + "max", maxs.get(id));
    }
    e.putBoolean(prefix() + "calibrated", true).apply();
    calibrating = false;
    saveMapping();
    log("Calibration saved for " + deviceName);
    toast("Calibration saved. Return controls to center.");
    render();
  }

  private float normalized(int id, boolean invert) {
    float v = axes.getOrDefault(id, centers.getOrDefault(id, 0f)), c = centers.getOrDefault(id, 0f);
    float extent = v >= c ? maxs.getOrDefault(id, 1f) - c : c - mins.getOrDefault(id, -1f);
    float n = extent > .001f ? Math.max(-1, Math.min(1, (v - c) / extent)) : 0;
    if (Math.abs(n) <= deadzone) n = 0;
    else n = Math.copySign((Math.abs(n) - deadzone) / (1 - deadzone), n);
    return invert ? -n : n;
  }

  private float forward() {
    if (radioActive) return radioState.elevator;
    return normalized(forwardAxis, invertForward);
  }

  private float turn() {
    if (radioActive) return radioState.aileron;
    return normalized(turnAxis, invertTurn);
  }

  @Override
  public boolean dispatchGenericMotionEvent(MotionEvent e) {
    inputTrace.motion(e);
    if (nativeLive) return super.dispatchGenericMotionEvent(e);
    if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        && e.getAction() == MotionEvent.ACTION_MOVE) {
      InputDevice d = e.getDevice();
      if (d != null && (inputId < 0 || InputDevice.getDevice(inputId) == null)) selectInput(d);
      if (e.getDeviceId() != inputId) return super.dispatchGenericMotionEvent(e);
      for (InputDevice.MotionRange r : d.getMotionRanges()) {
        int id = r.getAxis();
        float v = e.getAxisValue(id);
        axes.put(id, v);
        if (calibrating) {
          mins.put(id, Math.min(v, mins.getOrDefault(id, v)));
          maxs.put(id, Math.max(v, maxs.getOrDefault(id, v)));
        }
      }
      inputAt = SystemClock.elapsedRealtime();
      inputEvents++;
      if (radioActive) {
        learnRadioAxis();
        updateRadio();
        return true;
      }
      if (!enableHeld()) {
        disarm("Enable released");
        sendFrame();
      }
      if (cycleAxis >= 0) {
        boolean pressed = axisHeld(cycleAxis, cycleAxisLow);
        if (pressed && !cycleAxisWasHeld) requestLaunch();
        cycleAxisWasHeld = pressed;
      }
      return true;
    }
    return super.dispatchGenericMotionEvent(e);
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent e) {
    inputTrace.key(e);
    if (nativeLive) return super.dispatchKeyEvent(e);
    if (e.getDeviceId() == inputId
        && (KeyEvent.isGamepadButton(e.getKeyCode())
            || (e.getKeyCode() >= KeyEvent.KEYCODE_DPAD_UP
                && e.getKeyCode() <= KeyEvent.KEYCODE_DPAD_CENTER))) {
      int key = e.getKeyCode();
      boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
      boolean edge = down && !keys.contains(key) && e.getRepeatCount() == 0;
      if (down) keys.add(key);
      else keys.remove(key);
      inputAt = SystemClock.elapsedRealtime();
      inputEvents++;
      seenKeys.add(key);
      if (radioActive) {
        updateRadio();
        log("KEY " + KeyEvent.keyCodeToString(key) + " " + (down ? "down" : "up"));
        return true;
      }
      if (enableAxis < 0 && key == enableKey && !down) {
        disarm("Enable released");
        sendFrame();
      }
      if (cycleAxis < 0 && edge && key == launchKey) requestLaunch();
      log("KEY " + KeyEvent.keyCodeToString(key) + " " + (down ? "down" : "up"));
      return true;
    }
    return super.dispatchKeyEvent(e);
  }

  private boolean freshTelemetry() {
    return ble.ready && telemetryAt > 0 && SystemClock.elapsedRealtime() - telemetryAt < 200;
  }

  private void requestArm() {
    if (nativeProbeMode) {
      disarm("Native probe mode blocks arming");
      return;
    }
    if (radioActive) {
      requestRadioArm();
      return;
    }
    if (demo) {
      toast("DEMO: inputs displayed; arming and Bluetooth commands disabled.");
      return;
    }
    String why = null;
    if (!foreground || !hasWindowFocus()) why = "App must have focus";
    else if (inputAt == 0 || InputDevice.getDevice(inputId) == null)
      why = "No live game controller input";
    else if (!axes.containsKey(forwardAxis) || !axes.containsKey(turnAxis))
      why = "Move both mapped gimbal axes first";
    else if (!Float.isFinite(forward()) || !Float.isFinite(turn()))
      why = "Invalid controller values";
    else if (calibrating || !prefs.getBoolean(prefix() + "calibrated", false))
      why = "Calibrate mapped axes first";
    else if ((enableAxis < 0 && cycleAxis < 0 && enableKey == launchKey)
        || (enableAxis >= 0
            && (enableAxis == forwardAxis || enableAxis == turnAxis || enableAxis == cycleAxis))
        || (cycleAxis >= 0 && (cycleAxis == forwardAxis || cycleAxis == turnAxis))
        || forwardAxis == turnAxis) why = "Use distinct axis and button mappings";
    else if (!enableHeld()) why = "Hold mapped enable first";
    else if (Math.abs(forward()) > .02f || Math.abs(turn()) > .02f) why = "Center gimbals first";
    else if (!freshTelemetry()) why = "No compatible fresh robot telemetry";
    else if ((robotFlags & readyMask()) != readyMask() || (robotFlags & 8) != 0 || lastFaults != 0)
      why = "Robot must report ready, referenced and fault-free";
    else if ((capabilities & (1 << mode)) == 0) why = "Robot does not support this mode";
    if (why != null) {
      toast(why);
      log("Arm blocked: " + why);
      return;
    }
    armedRequest = true;
    armAt = SystemClock.elapsedRealtime();
    log("Deliberate arm request; waiting for robot confirmation");
    render();
  }

  private void requestLaunch() {
    if (nativeProbeMode) return;
    if (radioActive) {
      requestRadioJump();
      return;
    }
    if (!armedRequest
        || !freshTelemetry()
        || (robotFlags & 4) == 0
        || !enableHeld()
        || Math.abs(forward()) > .02f
        || Math.abs(turn()) > .02f) {
      toast("Cycle blocked: confirmed arm, held enable and centered controls required.");
      return;
    }
    if (mode == 0 || (capabilities & (1 << mode)) == 0) {
      toast("Select a supported cycle mode first.");
      return;
    }
    launchPending = true;
    log("One-shot cycle requested");
  }

  private void disarm(String why) {
    maintenancePending = false;
    maintenanceCancelPending = false;
    boolean changed = armedRequest || launchPending;
    armedRequest = false;
    launchPending = false;
    radioArming.reset();
    lastReason = why;
    if (changed) log("DISARM: " + why);
  }

  private void acceptTelemetry(byte[] b) {
    if (!Protocol.valid(b, 0x81) || Protocol.u16(b, 16) != session) {
      badFrames++;
      return;
    }
    int seq = Protocol.u16(b, 4);
    if (telemetrySeq >= 0) {
      int delta = (seq - telemetrySeq) & 65535;
      if (delta == 0 || delta >= 32768) {
        badFrames++;
        return;
      }
    }
    telemetrySeq = seq;
    telemetryAt = SystemClock.elapsedRealtime();
    rxCount++;
    robotFlags = b[6] & 255;
    leftDeg = Protocol.s16(b, 8) / 100.f;
    rightDeg = Protocol.s16(b, 10) / 100.f;
    volts = Protocol.u16(b, 12) / 1000.f;
    lastFaults = b[14] & 255;
    capabilities = b[15] & 255;
    int commissioningKey =
        (capabilities << 16) | (lastFaults << 8) | (robotFlags & (1 | 2 | 8 | 32 | 64));
    if (directionTest() && commissioningKey != commissioningFlags) {
      commissioningFlags = commissioningKey;
      commissioningEvidence =
          "t="
              + telemetryAt
              + " capability="
              + capabilities
              + " flags=0x"
              + Integer.toHexString(robotFlags)
              + " watchdog="
              + ((robotFlags & 32) != 0
                  ? "PENDING"
                  : (robotFlags & 1) != 0
                      ? dualPosition() ? "AUTO CONFIG VERIFIED" : "BOTH QUALIFIED"
                      : "UNCONFIRMED")
              + " reference="
              + ((robotFlags & 2) != 0 ? "VALID" : "NOT VALID / NOT USED")
              + " diagnostic="
              + motorDiagnostic();
      log(commissioningEvidence);
      if (dualPosition() && lastFaults != 0) {
        motorFaultHistory.addLast(commissioningEvidence);
        while (motorFaultHistory.size() > 16) motorFaultHistory.removeFirst();
      }
    }
    if (armedRequest
        && ((b[7] & 255) != mode
            || (!benchMode() && (capabilities & (1 << mode)) == 0)
            || (robotFlags & readyMask()) != readyMask()
            || (directionTest() && (robotFlags & 32) != 0)
            || (robotFlags & 8) != 0
            || lastFaults != 0)) disarm("Robot readiness/reference/fault changed");
    if (armedRequest && (robotFlags & 4) == 0 && SystemClock.elapsedRealtime() - armAt > 500)
      disarm("Robot did not retain/confirm arm");
  }

  private void sendFrame() {
    if (nativeProbeMode) return;
    boolean sourceFresh = liveInputFresh();
    if (!sourceFresh) {
      disarm("Input source stopped/stale; command heartbeat suspended");
      if (inputStopSent) return;
    } else inputStopSent = false;
    if (!Float.isFinite(forward()) || !Float.isFinite(turn()))
      disarm(
          radioActive
              ? "RC inputs unmapped or calibration incomplete"
              : "Invalid controller values");
    if (radioActive && (capabilities & 16) == 0) launchPending = false;
    if (maintenancePending
        && (!directionTest() || radioState.sb != 0 || !sourceFresh || !freshTelemetry()))
      maintenancePending = false;
    if (demo || !ble.ready || ble.busy) return;
    boolean allowed =
        armedRequest
            && foreground
            && hasWindowFocus()
            && driveEnable()
            && freshTelemetry()
            && liveInputFresh();
    byte[] frame =
        radioActive
            ? Protocol.radioCommand(
                sequence,
                session,
                allowed,
                radioState.sb,
                mode,
                forward(),
                turn(),
                radioState.throttle,
                radioState.height,
                radioArming.spinnerLowQualified && (capabilities & 8) != 0 && (robotFlags & 4) != 0,
                launchPending && allowed)
            : Protocol.command(
                sequence, session, allowed, mode, forward(), turn(), launchPending && allowed);
    if (maintenancePending) frame = Protocol.watchdogCheck(sequence, session, mode);
    if (maintenanceCancelPending && directionTest())
      frame = Protocol.directionStop(sequence, session, mode, radioState.sb);
    if (ble.send(frame)) {
      maintenancePending = false;
      maintenanceCancelPending = false;
      if (!sourceFresh) inputStopSent = true;
      sequence = (sequence + 1) & 65535;
      txCount++;
      lastWrite = SystemClock.elapsedRealtime();
      launchPending = false;
    }
  }

  private final Runnable pump =
      new Runnable() {
        public void run() {
          long now = SystemClock.elapsedRealtime();
          if (foreground) {
            consumeNativeInputs();
            if (inputMonitor != null && now - monitorAt >= 40) {
              updateInputMonitor(now);
              monitorAt = now;
            }
            if (armedRequest && (!freshTelemetry() || !driveEnable() || !liveInputFresh()))
              disarm("Input enable/device or telemetry lost");
            if (armedRequest && (robotFlags & 4) == 0 && now - armAt > 500)
              disarm("Arm confirmation timeout");
            if (ble.busy && now - ble.writeAt >= 150) {
              disarm("BLE write timeout");
              ble.disconnect();
              linkStatus = "Write timeout; reconnect manually";
              log(linkStatus);
            }
            if (now - lastWrite >= 20) sendFrame();
            if (now - lastRender >= 100) {
              render();
              lastRender = now;
              if (inputAt > lastInputLog) {
                log(
                    String.format(
                        Locale.US,
                        "INPUT forward=%.4f turn=%.4f drive_enable=%s axes=%s keys=%s",
                        forward(),
                        turn(),
                        driveEnable(),
                        axes,
                        keys));
                lastInputLog = inputAt;
              }
            }
          }
          handler.postDelayed(this, 10);
        }
      };

  private void render() {
    if (headline == null) return;
    boolean actualRun = freshTelemetry() && (robotFlags & 4) != 0;
    boolean actualFault = freshTelemetry() && ((robotFlags & 8) != 0 || lastFaults != 0);
    headline.setText(
        "JUMPING FLASH  /  AX12  •  "
            + (demo
                ? "DEMO / NO OUTPUT"
                : actualFault
                    ? "FAULT"
                    : actualRun
                        ? armedRequest ? "LEG ARMED" : "STOPPING"
                        : freshTelemetry() ? "DISARMED" : "STANDBY"));
    if (summary != null)
      summary.setText(
          deviceName
              + "\n"
              + (demo ? "DEMO — gimbals only, no robot connection" : linkStatus)
              + "\nReference: "
              + (freshTelemetry() ? ((robotFlags & 2) != 0 ? "VALID" : "INVALID") : "UNKNOWN")
              + String.format(
                  Locale.US,
                  "   Legs: %s   Battery: %s",
                  freshTelemetry()
                      ? singleLeg()
                          ? String.format(Locale.US, "%.1f° (single leg)", leftDeg)
                          : String.format(Locale.US, "%.1f° / %.1f°", leftDeg, rightDeg)
                      : "—",
                  freshTelemetry() && volts > 0 ? String.format(Locale.US, "%.2f V", volts) : "—")
              + "\n"
              + lastReason);
    if (modeLabel != null)
      modeLabel.setText(
          "Requested mode: "
              + new String[] {"Manual", "Jump", "Gyro walk"}[mode]
              + " • "
              + ((capabilities & (1 << mode)) != 0 && freshTelemetry()
                  ? "robot reports support"
                  : "not available on connected firmware"));
    if (linkLabel != null)
      linkLabel.setText(
          linkStatus + (freshTelemetry() ? "\nRobot online. Arming stays under SB control." : ""));
    if (calLabel != null)
      calLabel.setText(
          calibrating
              ? "CALIBRATING — sweep endpoints, then save"
              : "Calibration: "
                  + (prefs.getBoolean(prefix() + "calibrated", false)
                      ? "saved"
                      : "required before arming"));
    if (rawView != null) {
      StringBuilder s = new StringBuilder();
      s.append(deviceName)
          .append("\n")
          .append(
              String.format(
                  Locale.US,
                  "Forward %.3f  Turn %.3f  Enable %s\n",
                  forward(),
                  turn(),
                  driveEnable()))
          .append("Input events ")
          .append(inputEvents)
          .append("  last change ")
          .append(inputAt == 0 ? "never" : (SystemClock.elapsedRealtime() - inputAt) + " ms ago")
          .append("\n");
      for (int id : axes.keySet())
        s.append(
            String.format(
                Locale.US,
                "%s  raw %+.4f  min %+.3f  center %+.3f  max %+.3f\n",
                MotionEvent.axisToString(id),
                axes.get(id),
                mins.getOrDefault(id, 0f),
                centers.getOrDefault(id, 0f),
                maxs.getOrDefault(id, 0f)));
      s.append("Buttons: ");
      for (int key : keys) s.append(KeyEvent.keyCodeToString(key)).append(' ');
      if (tab == 3) {
        s.append("\nBLE TX ")
            .append(txCount)
            .append(" RX ")
            .append(rxCount)
            .append(" rejected ")
            .append(badFrames)
            .append("\n");
        int skip = Math.max(0, logs.size() - 8), i = 0;
        for (String l : logs) if (i++ >= skip) s.append(l).append('\n');
      }
      if (tab == 3) s.append("\n").append(inputTrace.recent());
      rawView.setText(s);
    }
    if (radioActive) renderRadio();
    if (singleLeg() && modeLabel != null)
      modeLabel.setText(
          "SINGLE LEG TEST • SB: "
              + (radioState.sb < 0
                  ? "UNKNOWN"
                  : radioState.sb == 1 ? "LEG ARMED" : RadioProfile.ARM_NAMES[radioState.sb])
              + "\nElevator: extend/retract; neutral holds. 1 A / 1 rad/s, 0–80°."
              + "\nSC gait / S2 height / SD jump / spinner: unavailable; steering ignored."
              + "\nLeg: "
              + (freshTelemetry() && (robotFlags & 4) != 0 ? "ARMED" : "DISABLED"));
    if (nativeLive && summary != null)
      summary.append(
          "\nInternal input: "
              + (liveInputFresh() ? "FRESH" : "STOPPED / STALE")
              + " • "
              + nativeRadio.status());
    if (nativeToggle != null)
      nativeToggle.setText(
          nativeEnabled ? "Internal inputs ON • turn OFF" : "Internal inputs OFF • turn ON");
    if (nativeLiveStatus != null) nativeLiveStatus.setText(nativeRadio.status());
    if (nativeProbeMode) headline.setText("NATIVE PROBE • ROBOT OUTPUT BLOCKED");
    if (nativeProbeView != null && nativeProbe != null)
      nativeProbeView.setText(nativeProbe.snapshot());
    if (mission != null) {
      boolean linked = freshTelemetry(), active = linked && (robotFlags & 4) != 0;
      mission.linked = linked;
      mission.directionTest = directionTest();
      mission.referenced = linked && (robotFlags & 2) != 0;
      mission.armed = active;
      mission.fault = actualFault;
      mission.stopping = active && !armedRequest;
      mission.degrees = leftDeg;
      mission.invalidate();
      missionStatus.setText(
          actualFault
              ? "ROBOT FAULT"
              : active
                  ? (armedRequest ? "LEG ARMED" : "STOPPING")
                  : linked ? "LINKED / DISARMED" : "LET’S GET CONNECTED");
      missionStatus.setTextColor(actualFault ? Color.rgb(215, 53, 73) : active ? ACCENT : TEXT);
      missionDetails.setText(
          "ROBOT  "
              + (linked
                  ? "Connected"
                  : ble.pairing()
                      ? "Pairing…"
                      : ble.ready ? "Waiting for telemetry" : "Not connected")
              + "\nINPUTS  "
              + (liveInputFresh() ? "Live" : nativeEnabled ? "Waiting for radio" : "Switched OFF")
              + "\nZERO  "
              + (linked
                  ? ((robotFlags & 2) != 0 ? "Set" : "Set retracted pose with B")
                  : "Connect robot first")
              + "\nBATTERY  "
              + (linked && volts > 0 ? String.format(Locale.US, "%.1f V", volts) : "—"));
      modeLabel.setText(
          "SB  "
              + (radioState.sb < 0
                  ? "Waiting for input"
                  : radioState.sb == 0
                      ? "DISARMED"
                      : radioState.sb == 1 ? "LEG ARMED selected" : "FULL ARM selected / leg only")
              + "\n"
              + (actualFault
                  ? "Motor fault. Keep SB DISARMED and inspect the rig."
                  : !liveInputFresh()
                      ? "Waiting for internal radio input. See Pit Tools if this persists."
                      : !linked
                          ? "Find your Stick on CONNECT."
                          : (robotFlags & 2) == 0
                              ? "Retract the leg; hold Stick B for 2 seconds with SB DISARMED."
                              : active
                                  ? "Neutral holds. SB DISARMED stops."
                                  : radioState.sb == 0
                                      ? "Ready. Center gimbals, then select LEG ARMED."
                                      : lastReason));
    }
    if (missionTitle != null)
      missionTitle.setText(
          dualPosition()
              ? "TWO LEGS / POSITION HOLD"
              : directionTest() ? "TWO MOTORS / DIRECTION TEST" : "SINGLE LEG / FLIGHT DECK");
    if (driveHint != null)
      driveHint.setText(
          dualPosition()
              ? "ELEVATOR  Extend / retract BOTH    •    NEUTRAL  Hold    •    SB  Enable / disable"
              : directionTest()
                  ? "ELEVATOR  Spin direction    •    NEUTRAL  Zero speed    •    SC  Right / left"
                      + " / both    •    SB  Enable / disable"
                  : "ELEVATOR  Move the leg     •     NEUTRAL  Hold position     •     SB  Disarm /"
                      + " arm");
    if (directionTest() && mission != null) {
      boolean live = freshTelemetry();
      boolean checkFailed = live && (robotFlags & 64) != 0;
      boolean pending = live && (robotFlags & 32) != 0, running = live && (robotFlags & 4) != 0;
      missionStatus.setText(
          !live
              ? "ROBOT STATUS STALE"
              : actualFault
                  ? "MOTOR / CAN FAULT"
                  : pending
                      ? dualPosition()
                          ? "CONNECTING MOTORS"
                          : running ? "WATCHDOG CHECK" : "CHECK WATCHDOGS"
                      : running
                          ? armedRequest ? "MOTORS ENABLED" : "STOPPING"
                          : dualPosition()
                              ? (robotFlags & 2) != 0
                                  ? "POSITION CONTROL READY"
                                  : "REFERENCE REQUIRED"
                              : "DIRECTION TEST READY");
      missionDetails.setText(
          "SELECTED  "
              + selectionName()
              + "\nWATCHDOGS  "
              + (!live
                  ? "UNCONFIRMED"
                  : checkFailed
                      ? "NOT VERIFIED / RETRY"
                      : pending
                          ? dualPosition() ? "AUTOMATIC SETUP" : "CHECK REQUIRED"
                          : (robotFlags & 1) != 0
                              ? dualPosition() ? "AUTO CONFIG VERIFIED" : "BOTH PASS"
                              : "UNCONFIRMED")
              + "\nINPUTS  "
              + (liveInputFresh() ? "Live" : "Waiting")
              + (dualPosition()
                  ? "\nREFERENCE  " + ((robotFlags & 2) != 0 ? "VALID" : "REQUIRED / INVALID")
                  : "\nZERO  Not used for this test")
              + (live
                  ? String.format(Locale.US, "\nTRAVEL  R %+.1f° / L %+.1f°", rightDeg, leftDeg)
                  : "\nTRAVEL  —")
              + (dualPosition() && lastFaults != 0 ? "\nFAULT  " + motorDiagnostic() : "")
              + "\nLOWER BATTERY  "
              + (live && volts > 0 ? String.format(Locale.US, "%.1f V", volts) : "—"));
      modeLabel.setText(
          !live
              ? "Motor state unconfirmed. Keep SB DISARMED and reconnect."
              : actualFault
                  ? "Stop requested for both motors; inspect CAN and both power supplies."
                  : pending
                      ? dualPosition()
                          ? "Keep SB DISARMED and center controls. Motor setup/recovery is"
                              + " automatic."
                          : "Keep SB DISARMED. Pit Tools → Check BOTH motor watchdogs."
                      : dualPosition()
                          ? ((robotFlags & 2) != 0
                              ? "BOTH fixed 0–80°. Elevator moves; neutral holds.\n"
                                  + "1 A / 1 rad/s. SB OFF disables; deliberate rearm required."
                              : "SB DISARMED. Place BOTH at retracted pose.\n"
                                  + "Release then hold Stick B 2s. Invalid zero: restart first.")
                          : "SC selects right / left / both.\n"
                              + "Elevator spins; neutral commands zero speed.\n"
                              + "Run guard ~80° / 2.5s. Cycle SB off to rearm.\n"
                              + "UNLOADED OUTPUTS ONLY — no leg mechanisms.");
    }
    if (directionTest() && connectionHelp != null)
      connectionHelp.setText(
          dualPosition()
              ? "DUAL POSITION • ID1 right / ID2 left\n"
                  + "Watchdogs automatic. SB DISARMED; place BOTH at retracted pose; release"
                  + " then hold Stick B 2s. Both zeros stay fixed until restart. Elevator moves"
                  + " both; neutral holds. Bare outputs first."
              : "DIRECTION TEST • ID1 right / ID2 left\n"
                  + "Keep both outputs unloaded. Pit Tools → Check BOTH motor watchdogs, with SB"
                  + " DISARMED and centered gimbals. Then SC selects motor(s) and elevator"
                  + " selects spin direction. No zero-setting step.");
    if (tab == 1 && rawView == null && radioInfo != null)
      radioInfo.setText(
          "Throttle "
              + percent(radioState.throttle)
              + "   •   Height dial "
              + percent(radioState.height)
              + "\nSC "
              + (radioState.sc < 0 ? "Waiting" : RadioProfile.STYLE_NAMES[radioState.sc])
              + "   •   SD "
              + (radioState.sd < 0 ? "Waiting" : radioState.sd == 0 ? "Low" : "High"));
    if (disconnectButton != null)
      disconnectButton.setVisibility(
          ble.ready || ble.pairing() || ble.connecting() ? View.VISIBLE : View.GONE);
    for (Map.Entry<String, Button> item : robotButtons.entrySet()) {
      boolean chosen = item.getKey().equals(prefs.getString("lastRobot", ""));
      boolean active = chosen && (ble.ready || ble.pairing() || ble.connecting());
      item.getValue().setEnabled(!active);
      item.getValue()
          .setText(
              active
                  ? (ble.ready ? "✓  Connected" : ble.pairing() ? "Pairing…" : "Connecting…")
                  : "Connect this Stick  →");
    }
    if (connectionSteps != null)
      connectionSteps.setText(
          freshTelemetry()
              ? "✓  STICK CONNECTED"
              : ble.pairing()
                  ? "02  PAIRING • ACCEPT ANDROID PROMPT"
                  : ble.ready
                      ? "03  WAITING FOR ROBOT STATUS"
                      : ble.connecting()
                          ? "03  CONNECTING TO YOUR STICK…"
                          : ble.scanning
                              ? "01  SEARCHING FOR YOUR STICK…"
                              : "01  FIND     →     02  PAIR     →     03  CONNECTED");
    updateInputMonitor(SystemClock.elapsedRealtime());
    if (sticks != null) sticks.invalidate();
  }

  private void toast(String s) {
    Toast.makeText(this, s, Toast.LENGTH_LONG).show();
  }

  @Override
  protected void onResume() {
    super.onResume();
    foreground = true;
    keys.clear();
    seenKeys.clear();
    axes.clear();
    radioState = new RadioProfile.Snapshot();
    cycleAxisWasHeld = true;
    disarm("Foreground resumed; deliberate rearm required");
    detectInput();
    ble.resume();
    if (nativeEnabled && !nativeProbeMode) resumeNativeInputs(++nativeResumeGeneration, 0);
  }

  @Override
  protected void onPause() {
    foreground = false;
    ++nativeResumeGeneration;
    if (nativeProbe != null) nativeProbe.stop();
    if (nativeRadio != null) nativeRadio.stop();
    if (nativeOwnerCheck != null) nativeOwnerCheck.setChecked(false);
    disarm("App left foreground");
    sendFrame();
    keys.clear();
    // Temporary system overlays can pause us without hiding the activity.
    // Disarm above, but retain GATT until the activity actually becomes invisible.
    super.onPause();
  }

  @Override
  protected void onStop() {
    ble.pause();
    super.onStop();
  }

  @Override
  public void onWindowFocusChanged(boolean focus) {
    super.onWindowFocusChanged(focus);
    if (!focus) {
      if (nativeProbe != null) nativeProbe.stop();
      if (nativeOwnerCheck != null) nativeOwnerCheck.setChecked(false);
      disarm("App lost focus");
      sendFrame();
      keys.clear();
      radioState = new RadioProfile.Snapshot();
      seenKeys.clear();
      axes.clear();
    }
  }

  @Override
  protected void onDestroy() {
    if (nativeProbe != null) nativeProbe.stop();
    if (nativeRadio != null) nativeRadio.stop();
    handler.removeCallbacksAndMessages(null);
    ((InputManager) getSystemService(INPUT_SERVICE)).unregisterInputDeviceListener(this);
    ble.destroy();
    super.onDestroy();
  }

  @Override
  public void onInputDeviceAdded(int id) {
    detectInput();
  }

  @Override
  public void onInputDeviceChanged(int id) {
    if (id == inputId) {
      disarm("Input device changed");
      keys.clear();
    }
  }

  @Override
  public void onInputDeviceRemoved(int id) {
    if (id == inputId) {
      disarm("Input device removed");
      sendFrame();
      keys.clear();
      inputId = -1;
      inputAt = 0;
      deviceName = "Game controller disconnected";
      log(deviceName);
    }
  }

  @Override
  public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
    super.onRequestPermissionsResult(request, permissions, results);
    if (request == 10) {
      Runnable action = pendingBluetoothAction;
      pendingBluetoothAction = null;
      boolean granted = results.length > 0;
      for (int result : results)
        granted &= result == android.content.pm.PackageManager.PERMISSION_GRANTED;
      if (granted && action != null) action.run();
      else {
        linkStatus =
            "Bluetooth permission is needed to find your Stick. Tap Find my Stick to try again.";
        render();
      }
    }
  }

  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (request == 11 || request == 12) {
      Runnable action = pendingBluetoothAction;
      pendingBluetoothAction = null;
      boolean accepted =
          request == 11
              ? result == RESULT_OK
              : ((android.location.LocationManager) getSystemService(LOCATION_SERVICE))
                  .isLocationEnabled();
      if (accepted && action != null) action.run();
      else {
        linkStatus =
            "Enable Bluetooth and Android Location to find your Stick, then tap Find my Stick.";
        render();
      }
      return;
    }
    if (request == 30 && result == RESULT_OK && data != null) {
      try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
        if (out == null) throw new IOException("No output stream");
        String header =
            "Jumping Flash AX12 0.7.1\nNamed profile "
                + radioMappingText()
                + "\nDevice "
                + deviceName
                + "\nDescriptor "
                + descriptor
                + "\nCalibration min="
                + mins
                + " center="
                + centers
                + " max="
                + maxs
                + "\nAndroid "
                + Build.VERSION.RELEASE
                + " / "
                + Build.MODEL
                + "\nLAST COMMISSIONING TELEMETRY (historical, not current freshness): "
                + commissioningEvidence
                + "\nMapping forward="
                + forwardAxis
                + " turn="
                + turnAxis
                + " enable="
                + enableKey
                + " cycle="
                + launchKey
                + "\n";
        out.write(header.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        for (String event : motorFaultHistory)
          out.write(
              ("MOTOR FAULT HISTORY (historical): " + event + "\n")
                  .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        if (nativeProbe != null)
          out.write(nativeProbe.dump().getBytes(java.nio.charset.StandardCharsets.UTF_8));
        if (nativeRadio != null)
          out.write(
              ("\nLIVE INTERNAL SOURCE selected="
                      + nativeLive
                      + " fresh="
                      + liveInputFresh()
                      + " status="
                      + nativeRadio.details()
                      + "\n")
                  .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        out.write(
            (InputTrace.inventory() + "\n" + inputTrace.dump())
                .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        for (String l : logs)
          out.write((l + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
        toast("Diagnostic log exported");
      } catch (IOException | SecurityException e) {
        toast("Export failed: " + e.getMessage());
      }
    }
  }

  private final class StickView extends View {
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

    StickView() {
      super(MainActivity.this);
    }

    protected void onDraw(Canvas c) {
      super.onDraw(c);
      float x = getWidth() * .5f,
          y = getHeight() * .5f,
          r = Math.min(getHeight() * .38f, getWidth() * .2f);
      p.setColor(PANEL);
      c.drawCircle(x, y, r, p);
      p.setColor(TEXT);
      p.setStrokeWidth(2);
      c.drawLine(x - r, y, x + r, y, p);
      c.drawLine(x, y - r, x, y + r, p);
      p.setColor(ACCENT);
      c.drawCircle(
          x + (Float.isFinite(turn()) ? turn() : 0) * r,
          y - (Float.isFinite(forward()) ? forward() : 0) * r,
          12,
          p);
      p.setColor(TEXT);
      p.setTextSize(22);
      c.drawText(
          "Forward " + signedDisplay(forward()) + "   Turn " + signedDisplay(turn()), 12, 28, p);
    }
  }
}
