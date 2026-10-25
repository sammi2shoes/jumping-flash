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
  private static final int BG = Color.rgb(15, 21, 31),
      PANEL = Color.rgb(26, 35, 49),
      TEXT = Color.rgb(230, 237, 245),
      ACCENT = Color.rgb(61, 215, 183);
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
    summary = text("", 18);
    body.addView(summary);
    body.addView(
        nativeToggle =
            button(
                nativeEnabled ? "Internal inputs ON • turn OFF" : "Internal inputs OFF • turn ON",
                () -> {
                  setNativeEnabled(!nativeEnabled);
                  showTab(0);
                }));
    sticks = new StickView();
    body.addView(sticks, new LinearLayout.LayoutParams(-1, 160));
    radioInfo = text("", 16);
    body.addView(radioInfo);
    modeLabel =
        text(
            "SB controls arming • SC selects movement style • SD toggles request full-power jump",
            16);
    body.addView(modeLabel);
    body.addView(
        button(
            "Disarm / cancel pending jump",
            () -> {
              disarm("Operator STOP; cycle SB to disarmed before rearming");
              sendFrame();
            }));
    body.addView(
        text(
            "CH3 spinner throttle is blocked in SB disarmed/drive-only. Enter full arm with"
                + " throttle low. SD triggers on either edge in either armed state, in any movement"
                + " style. No jump is queued while disarmed or disconnected. Robot firmware must"
                + " implement the requested actions.",
            14));
    body.addView(
        button(
            demo ? "Exit DEMO" : "Enter DEMO • inputs only / no robot output",
            () -> {
              disarm("Demo changed");
              ble.close();
              demo = !demo;
              linkStatus = demo ? "DEMO — inputs only" : "Disconnected";
              showTab(0);
            }));
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
    if (!freshTelemetry() || (robotFlags & 3) != 3 || (robotFlags & 8) != 0 || lastFaults != 0)
      return "Fresh ready, referenced, fault-free robot required";
    if (!singleLeg() && (capabilities & (1 << mode)) == 0)
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
    nativeRadio = new NativeRadioInput(new NativeUartProbe.DevicePort());
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
    log("AX12 app 0.4.1. Named RC profile; robot-side spinner/jump outputs not implemented.");
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
    t.setPadding(8, 6, 8, 6);
    return t;
  }

  private Button button(String s, Runnable action) {
    Button b = new Button(this);
    b.setText(s);
    b.setAllCaps(false);
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
    LinearLayout root = new LinearLayout(this);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setFitsSystemWindows(true);
    root.setBackgroundColor(BG);
    root.setPadding(12, 8, 12, 8);
    LinearLayout top = row();
    headline = text("JUMPING FLASH  •  AX12", 20);
    top.addView(headline, new LinearLayout.LayoutParams(0, -2, 1));
    top.addView(button("STOP", this::operatorStop));
    root.addView(top);
    LinearLayout nav = row();
    addButton(nav, "Drive", () -> showTab(0));
    addButton(nav, "Controls", () -> showTab(1));
    addButton(nav, "Bluetooth", () -> showTab(2));
    addButton(nav, "Diagnostics", () -> showTab(3));
    addButton(nav, "Native inputs", () -> showTab(4));
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
    if (which == 1) controls();
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
    linkLabel = text("", 16);
    body.addView(linkLabel);
    body.addView(
        text(
            "This APK uses the Jumping Flash BLE service. MotorRecovery v0.16 uses Bluepad32 HID"
                + " and cannot connect to this protocol. A companion Stick firmware build is still"
                + " required. Pairing alone never enables motors.",
            16));
    LinearLayout r = row();
    addButton(
        r,
        "Scan BLE",
        () -> {
          if (nativeProbeMode) {
            toast("Exit native probe mode first");
            return;
          }
          if (demo) {
            toast("Exit demo first");
            return;
          }
          devices.clear();
          ble.scan();
        });
    addButton(r, "Choose discovered device", this::chooseBle);
    addButton(
        r,
        "Disconnect",
        () -> {
          disarm("Manual disconnect");
          sendFrame();
          ble.close();
        });
    body.addView(r);
    body.addView(
        button(
            "Android Bluetooth settings",
            () -> startActivity(new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS))));
  }

  private void operatorStop() {
    if (nativeProbe != null) nativeProbe.stop();
    disarm("Operator STOP");
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
    body.addView(text("AX12 internal inputs • live single-leg control", 20));
    body.addView(
        text(
            "Initialize RadioMasterAX once, then Force stop it in Android settings. Remove the"
                + " DSC-to-USB cable. Live input uses the verified internal ttyS0 reports; no UART"
                + " writes. Inputs start by default and resume automatically after backgrounding."
                + " Turn them OFF before opening RadioMasterAX. Verify gimbals centered, throttle"
                + " OFF and SB DISARMED on the dashboard before connecting the rig.",
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
    else if ((robotFlags & 3) != 3 || (robotFlags & 8) != 0 || lastFaults != 0)
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
    capabilities = b[15] & 63;
    if (armedRequest
        && ((b[7] & 255) != mode
            || (!singleLeg() && (capabilities & (1 << mode)) == 0)
            || (robotFlags & 3) != 3
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
    if (ble.send(frame)) {
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
    headline.setText(
        demo
            ? "JUMPING FLASH  •  DEMO / NO OUTPUT"
            : "JUMPING FLASH  •  AX12  /  "
                + (armedRequest && freshTelemetry() && (robotFlags & 4) != 0
                    ? "ARMED"
                    : "DISARMED"));
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
          linkStatus
              + "\nDiscovered BLE devices: "
              + devices.size()
              + "   Compatible telemetry: "
              + (freshTelemetry() ? "fresh" : "absent/stale"));
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
    ble.close();
    super.onPause();
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
    ble.close();
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
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (request == 30 && result == RESULT_OK && data != null) {
      try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
        if (out == null) throw new IOException("No output stream");
        String header =
            "Jumping Flash AX12 0.4.1\nNamed profile "
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
        if (nativeProbe != null)
          out.write(nativeProbe.dump().getBytes(java.nio.charset.StandardCharsets.UTF_8));
        if (nativeRadio != null)
          out.write(
              ("\nLIVE INTERNAL SOURCE selected="
                      + nativeLive
                      + " fresh="
                      + liveInputFresh()
                      + " status="
                      + nativeRadio.status()
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
