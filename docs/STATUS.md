# Current project status

## Temporary-overlay BLE retention — APK v0.6.3

[Change and hardware check](AX12-OVERLAY-CONNECTION.md): temporary Activity pause disarms but retains GATT; full stop still disconnects. Native input pause/resume and deliberate rearm are retained. Physical overlay/USB acceptance pending. The user confirms both outputs remain bare, correct two-motor movement and DIRECTION TEST READY (both watchdogs qualified through the firmware gate).

## Cold-start discovery probe — APK v0.6.2

The v0.6.1 cold-boot log verified921600/raw UART setup but received zero bytes. v0.6.2 adds at most three fixed stock link-discovery packets before any accepted control report, plus probe/RX diagnostic export. No unknown model/RF configuration traffic or motor behavior changes. [Evidence, scope and cold-boot test](AX12-COLD-START-PROBE.md). Build/lint and67 Android tests pass. The user reported live inputs immediately on the first v0.6.2 cold boot, then passed all three requested repeat cold-boot checks without opening RadioMaster. Cold-start input recovery is bench-confirmed across these four runs; interruption recovery and sustained competition reliability are not established.

## Native-input startup fix and live monitor — APK v0.6.1

A user screenshot showed the live input fd at9600baud/canonical mode instead of the known921600/raw stream. Live initialization now sets and reads back921600/raw8N1 itself, flushes buffered input, and reports LIVE only for verified fresh source packets. Passive capture still preserves settings. Tools now includes live gimbal plots, six channel/dial bars, SB/SC/SD indicators, report age and optional42-source readout. [Behavior, host validation and required hardware acceptance](AX12-INPUT-STARTUP.md). No Stick reflash or motor-profile change. The user reports direction is correct on both bare motor sides; no loaded reliability/timing claim follows.

Future requested UI: [CAD-based input mapping with separate commanded/measured motion](CAD-INPUT-MAPPING-ROADMAP.md).

## Two-motor unloaded direction test

Motor ID1 is RIGHT; newly updated RS05 ID2 is LEFT, with mirrored linkage. User currently has no mechanism/springs on the outputs and explicitly requested no reference-setting for this stage. [AX12DirectionTest v0.1 + APK v0.6.0](AX12-DIRECTION-TEST.md) use bounded velocity commands only, with SC selecting right/left/both. No zero, position target or position hold. Both motors are supervised, watchdog checks are requested in Pit Tools, and normal motion is capped near80° measured rotation /2.5sec per arm at1A/up to1rad/s. App-only firmware update preserves the existing partition/NVS layout. This new pair is compiled/simulated, not yet hardware accepted.

The user reported the previous single-leg AX12 path passed live input, motor motion, neutral hold, SB disarm, Bluetooth-loss stop and no restart after reconnect; ankle spring was disconnected. Updated printed feet prevented spring testing.

## AX12 single-leg integration — new test build

APK v0.5.0 adds a Jumping Flash–inspired flight deck, simplified single-leg Controls, Pit Tools for advanced/legacy setup and guided service-filtered Bluetooth connection. Android pairing automatically continues the original connection request; Disconnect cancels it. Last robot is remembered for one-tap manual reconnect. Firmware/limits are unchanged. [Previews and flow](AX12-UI-v0.5.md). Build, lint and56 Android tests pass; physical BLE acceptance is pending.

APK v0.4.1 makes internal inputs default-on with a remembered OFF switch, no repeated confirmation checkbox, continuous reading across focus dialogs/STOP, and automatic foreground resume. Backgrounding still disarms/closes BLE; input recovery never automatically rearms. Stick v0.1 is unchanged.

APK v0.5.0 and AX12SingleLeg v0.1 implement cable-free internal AX12 inputs and a matching Stick BLE peripheral for the one-motor rig. Manual reference, 0..80° travel, 1 A / 1 rad/s, neutral active hold and the tested CAN watchdog are preserved. SB disarms/arms; elevator moves the leg. No spinner, SD jump, height actuation or gait output. BLE/input loss clears motion and requires an SB disarmed cycle. Compiled/host-tested; this BLE pair awaits physical acceptance. See [install and test procedure](AX12-SINGLE-LEG.md). Existing MotorRecovery v0.16 remains the gamepad baseline.

## Verified on the user's bench

- Actual controller is M5StickC Plus2, not the originally discussed Plus/SE.
- IMU/display/Bluetooth diagnostic and controller-disconnect input zeroing passed.
- MiniCAN GPIO32TX/GPIO33RX; motor1, host126, classic1 Mbps CAN.
- RS05 position, velocity and voltage reads; a1 A, 5degree position move reached approximately4.98degrees and explicitly disabled afterward.
- Official motor update0.5.0.3 ->0.5.0.14 succeeded after using the OTA-panel Erase/update-entry step. This was not a factory reset or calibration.
- Standard watchdog A/B v4: timeout0 remained RUN after1,500 ms silence; timeout20000 became disabled. Explicit stops verified.
- Independent-observer physical disconnect run: disabled feedback1,028 ms after the last observed command, no explicit stop; fresh disabled feedback continued>20 seconds with no faults, missed frames, receive errors or invalid flag. User confirmed clean physical reconnection. No automatic restart was observed.

The tested watchdog setting is20,000 counts, nominal1second. This establishes the observed unloaded/zero-speed behavior and CAN-reported state; it does not measure loaded stopping distance, physical torque-removal time or certify competition compliance.

## MotorRecovery: current fixed-reference prototype

v0.16 requires a manually declared retracted pose (StickB held2seconds while disabled), captures zero once per startup, retains it through confirmed stops and Bluetooth reconnection, and blocks enable when motor/CAN reference trust is lost. R1 remains a held enable; neutral commands active position hold while held. Limits0..80deg; manual/Y slow profile1A/1rad/s/5rad/s^2. X-cycle extension profile3A/5rad/s/40rad/s^2 has a successful free-swing hardware trace; loaded push/jump performance is not validated. See MOTOR-RECOVERY.md for required startup and reference limitations. No fixed reference survives a Stick restart; no hardware home sensor exists yet.

The v0.6 log verifies both+/-20deg bench endpoints and R1-release disable. The user reports v0.9 direction, motion, controller-loss stop and reconnection worked with the leg system attached, superseding our assumption it remained unloaded. The user reports1A is sufficient to hold the lighter-spring test rig. v0.12 logs show fixed-angle operation after arming and completed cycles; startup repeatability and heavier-spring holding remain unmeasured. Host simulation covers fixed zero, held-enable stops/recovery, reference-invalid lockout and prior transport/readback guards.

The user reported REF INVALID on controller power-off in v0.10. v0.11 fixes a simulated reproduction where in-flight CAN operations aborted by controller loss were classified as CAN failure. The user subsequently confirmed that v0.11 fixed it; genuine transport failure/unconfirmed STOP still invalidates reference. The user also reports1A holds the lighter-spring test rig.

v0.12 adds a fresh controllerY-triggered one-motor retract/pause/extend/hold sequence at unchanged test limits. Endpoint arrival and continuous300ms retracted settling are required; a6second stage timeout stops, joystick overrides, and loss/stop cancels without resume. Host simulation and compilation pass. The user's v0.12 log confirms two completed cycles, retracted pause near0.09deg, extended hold at79.95–80.00deg, manual joystick override and R1-release disable during a later retraction. Moving samples show up to4.89deg target/measurement difference; higher-speed tracking and loaded launch performance are not established. See bench-logs/motor-recovery-v0.12-cycle-excerpt.txt.

v0.13 adds nominal10Hz timestamped filtered-Iq/velocity/tracking traces and nominal2Hz bus voltage. It fixes log alignment by comparing position with the command active at that read rather than the next command. The subsequent v0.14 log confirms filtered-Iq/velocity/voltage reads return finite data on this rig. No motion setting or control change was made.

The v0.13 hardware attempt stopped during arming on a0.000297rad position-target readback difference, before a trace was supplied. v0.14 allows at most0.0005rad for that register only, retains exact safety-setting checks, and clarifies the reference-invalid restart message. The exact observed hex pair and full regression suite pass. The subsequent v0.14 run completed without a readback/reference error.

The v0.14 trace has51 samples at median100ms intervals: max sampled tracking error2.551deg; max sampled filtered-Iq magnitude0.5745A; extended-hold mean magnitude0.200A; voltage24.141–24.176V. One cycle completed and R1 release confirmed disable/READY. Sampled current does not show sustained saturation, but does not bound brief peaks. See MOTOR-RECOVERY.md and bench-plots/motor-recovery-v0.14.png.

v0.15 preserves Y as slow and adds an X-triggered slow-retract/faster-extend trial. Verified high/low profile transitions, fast-stage10ms requested control updates, endpoint settling before low-current restoration and a1second software burst deadline are implemented. Fixed reference, watchdog and R1 stop remain. All host tests/build pass. A subsequent free-swing v0.15 run verified the high profile,514ms reported endpoint arrival, settling and restoration of low holding limits.

The user confirmed v0.15 testing was leg movement off the ground, despite feeling weak. Sampled burst filtered-Iq magnitude reached0.2533A against the3A setting; max sampled error8.904deg. No current/force/takeoff conclusion under body weight is established. A known-load foot-ground push is the next physical diagnostic; no speed/current increase was made from this result. See the v0.15 summary and comparison plot.

## Competition direction

User priority: minimum latency takes precedence over logging, optional reads and blocking verification. Competition launch/cutoff will need a predictable control path, cached feedback and pre-arm configuration checks; logging cannot refresh the motor watchdog independently of control health. Immediate recovery after the push is desired, with no extended dwell and no outward drive into the physical stops (passive coast/impact is accepted). Current v0.15 does not implement this final behavior. See CONTROL-ARCHITECTURE.md.

## Available and planned hardware

One RobStride 05 is currently available (bench ID1); the second leg motor is planned. Spinner ESC: brushless AM32, 100 A; PWM versus DShot undecided. Left/right motor assignment is not established.

## Agreed but not implemented

A repeatable physical retracted reference/jig or home sensor (manual declaration now implemented); physical CCW extension to80degrees; standing height TBD; two coordinated leg motors; jump mode; gyro-walk mode; spinner ESC output; global loss/recovery handling across every actuator; GUI/AX12 integration; calibrated IMU control and persistent onboard logging. See CONTROL-ARCHITECTURE.md and LEG-MECHANISM.md.

## Historical material

LegacySetup/LegacyParameterRead/MotorJog and the chronological RS05-FIRMWARE.md preserve the investigation of firmware0.5.0.3. Earlier4000=200 ms statements were not established on that firmware and are superseded by the tested20,000-count setting on0.5.0.14. Older sketches are not a deployment recipe. Raw uploaded logs include earlier invalid sessions followed by observer restarts; the final clean session is identified in WATCHDOG-DISCONNECT.md.

## Validation

scripts/check.sh runs all host simulations and Python checks. scripts/build.sh uses the pinned cloud toolchain; specify the firmware environment. Host simulations and builds do not replace device acceptance tests. No new mechanism motion should use the relative bench controller as an absolute leg-limit implementation.

## Next phase: RadioMaster AX12 Android APK

The AX12 is available for installing and testing an APK. Finish the v0.16 stroke timing capture, then prioritize the Android controller while the walking rig is being built over the next few days. First verify AX12 gimbal/switch input APIs and Android Bluetooth capabilities against its official documentation and actual device. The app needs mode selection, operator inputs, connection/reference/armed status and robot telemetry. A custom APK does not automatically present the HID interface expected by the current Bluepad32 firmware; select and document the app-to-Stick transport before implementing it. Link loss must stop functions, discard pending motion and require neutral inputs and deliberate rearming after reconnect. Spinner output and two-leg motion remain unimplemented.

v0.16 is a compiled, host-tested low-latency out-and-return build awaiting device traces. It removes synchronous reads/formatting from the fast window, retains3A/5rad/s and adds immediate retraction. See MOTOR-RECOVERY.md for changed fast-window cancellation and diagnostics.

## v0.16 device stroke capture

Two X out-and-return cycles reported recovered, restored the1A holding profile and later confirmed disable on R1 release. Outward arrival times were511.697ms and509.864ms;438/436 position commands, maximum command intervals2041/2038us,88 buffered samples each and zero dropped samples. Sampled fast-window spans were876.479/876.688ms (not exact full-cycle durations). Sampled outward velocity peaks295.899/289.692deg/s and return velocity minima-327.286/-329.122deg/s; sampled outward tracking errors up to10.745/10.658deg. The inward target is sent at the arrival threshold, but the trace shows substantial motor-side slowing before that threshold. This verifies approximately2ms host command scheduling for these runs, not controller-to-actuator latency or loaded launch performance. No new load condition was supplied with this log. Startup included a long wait without Bluetooth, followed by fresh reports, manual reference and normal arming. No mismatch, reference-invalid or fast-abort message appears in this capture.

Raw log, extracted fast CSV and per-run JSON are archived in bench-logs/motor-recovery-v0.16-*. Stroke data is now captured; next development focus is AX12 Android input/transport verification and APK work.

## AX12 Android app v0.1.0

The first substantial APK is in android/ax12, with standard Android joystick/button capture, saved per-device mapping, axis-based switches, center/endpoints calibration, inversion/deadzone, dashboard, BLE discovery/client transport, guarded arm requests, capability-gated mode requests, local no-output demo and exportable diagnostics. The user has the AX12 and reports its gimbals worked in an ORQA FPV simulator. Exact AX12 axes/switches remain an on-device check; no vendor SDK assumption was made.

The app is **not yet compatible with MotorRecovery v0.16's Bluepad32 HID path**. Matching BLE server firmware is the next integration step; motion stays blocked without a valid reference/readiness/mode-capability telemetry response. No jump/gyro gait or spinner implementation is claimed. No tested motor firmware was changed. See android/ax12/README.md and AX12-BLE-PROTOCOL.md. Public APK lives at releases/JumpingFlash-AX12-v0.1.0.apk.

APK validation: assembleDebug and lintDebug pass;11 JUnit/Robolectric tests pass (5 packet/CRC/input-sanitization cases and6 Android lifecycle/UI/enable/axis-switch/telemetry cases). All four tabs render under Android9 native graphics; dashboard/controls previews are in docs/android/screenshots. APK signature verified with apksigner, SHA256 published alongside APK. These are host/runtime checks, not physical AX12 or over-radio motor validation.

## AX12 v0.2.0: requested channel/switch profile

CH3 requests spinner throttle; CH1/CH2 steering/forward; S2 normalized standing height; SB disarmed/drive-only/full; SC hop/gyro/ambulate; SD either edge requests a full-power jump in either armed state independently of SC (user confirmed). Named per-device bindings support learned/manual axes, two-key three-position switches and a latching key. CH1/CH2 endpoint/center calibration and throttle OFF/full calibration are explicit. No Android axis IDs are inferred from RC channel numbers.

BLE v2 carries separate drive/spinner arm flags, throttle, height, style and independent full-jump intent in20bytes. Full-arm entry needs throttle-low qualification; drive-only always suppresses spinner. Reconnect/STOP requires SB disarmed before a new arm transition, SC changes while armed disarm, initial SD is a baseline and blocked edges are discarded. Receiver implementation (including actual ESC outputs, gaits and full-power launch behavior) remains pending; no motor firmware or tested limits were changed. AX12 hardware mappings are not yet verified.

v0.2.0 validation: assembleDebug and lintDebug pass;22 tests pass (10 named profile/arming/packet cases,5 existing protocol cases,7 Android runtime cases including an SB→drive arm→SD jump→SB disarm flow). Native-graphics screen renders refreshed; APK signature verified and checksum published. Physical AX12 mappings, radio behavior and receiver/motor/ESC integration remain unverified.

## AX12 dial investigation: v0.2.1 diagnostic capture

User reports gimbals and switches recognized, but neither dial changes v0.2.0's raw diagnostics. The handler only accepted selected-device joystick ACTION_MOVE, so that observation cannot exclude rotary/scroll, key-based or other-device input. v0.2.1 adds an independent all-source generic-motion/key trace and complete input-device/range inventory to diagnostic exports. A reset/probe button isolates a dial sweep. Working mappings/calibration, BLE v2 and all output/arming behavior are unchanged. No dial-to-height mapping is claimed until a real trace identifies the input representation; events observed only by diagnostics cannot arm, update freshness or command motion.

v0.2.1 build/lint and25 tests pass. Added tests verify rotary scroll from an unselected device and non-gamepad keys are captured diagnostically without becoming robot inputs, plus a real Activity export includes both the all-source trace and input inventory. APK signature verified. Physical dial event availability remains unverified.

## AX12 dial probe result and internal-channel research

The user's v0.2.1 export contains the complete input inventory and, after resetting the dial probe, only one selected-joystick ACTION_CANCEL. No dial-related generic-motion/key events were captured. Android9 identifies the control device as FLYSHARK Composite VCP and Joystick (VID0x2e3c/PID0x5750), exposing eight axes X/Y/Z/RX/RY/RZ/THROTTLE/RUDDER; no separate rotary input device is listed. The user confirms both dials visibly move in the radio's own channel monitor/calibration interface. This establishes functioning dial inputs in the radio software but not their availability through Android gamepad events. It does not rule out a non-InputDevice/vendor interface.

Independent research found at https://github.com/rmeadomavic/ax12-research/tree/b938319b6dcba5e3c694ef7aeede00a1ca40ffd2 documents internal UMBUS S1/S2 channels (research labels CH06/CH07), a native rcJoystickMap symbol, and the stock app's ownership of ttyS0. These are third-party findings, not a validated public AX12 SDK or confirmed menu option on the user's device. Internal channel labels must not be confused with the user's chosen CH1/CH2/CH3 controls. Next investigate stock simulator/joystick channel remapping to an unused HID axis; if unavailable, assess a supported vendor source interface. Do not open a competing ttyS0 reader or alter transmitter firmware as a routine diagnostic. No additional APK/dial workaround has been claimed from this finding.

Mads Tech video transcript reviewed: native ELRS persisted through closing the stock app and Android framework stop/start, but reboot interrupted it and stock-app startup appeared necessary for recovery. These observations do not make our Android BLE path independent of Android or provide a dial API. Preserve stock-app startup; investigate joystick source remapping or a cooperative vendor channel interface, with upstream input freshness, rather than taking over the serial port. Timestamped evidence and scope are recorded in AX12-ARCHITECTURE-NOTES.md.

## Supplied vendor APK inspected: RadioMasterAX.260908

Both analysis upload parts reassembled and15 selected hashes verified. Actual manifest/DEX/native AArch64 code and853 Qt resource entries inspected. The stock UI reads S2 from internal source index13 via AppRadioControl.getSrcCfgList/RcSrcGetRaw, separate from the user's cabled gamepad. This build has a non-exported boot service, not a channel API. Crucially, Lua recognizes s2/SB/SC/SD names but its getValue/getSourceValue backend only implements ele/thr/ail; a simple Lua dial bridge would return zero. rcJoystickMap is only a four-gimbal Mode1–4 permutation, not a proven generic HID remapper. Findings/hashes/addresses and remaining runtime-access questions are in AX12-APK-FINDINGS-260908.md. No transmitter or motor code was executed or patched.

## AX12 v0.3.0 passive internal UART prototype

Native-input screen, port access metadata, a tiny ARM64 read-only/exclusive JNI reader, bounded raw capture and provisional source decoding are implemented. It uses no root, UART writes or baud/config changes. Operator must force-stop the stock app before opening; TIOCEXCL prevents subsequent ordinary opens but does not evict pre-existing readers. Stock-app restore waits for our fd close. All robot sends/arm/jump actions are blocked in probe mode, which persists through app/activity restarts until explicit exit; probe values never feed robot controls. Existing mappings are retained.

Build/lint and44 JVM/Android-runtime tests pass, plus an actual C-core Linux pseudo-TTY test. Native source/binary hashes are checked in normal builds. User AX12 UART access/frames are not yet tested. The next result needed is the Native inputs capture, external cable removed, per AX12-NATIVE-PROBE.md. APK releases/JumpingFlash-AX12-v0.3.0.apk.
