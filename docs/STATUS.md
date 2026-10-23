# Current project status

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
