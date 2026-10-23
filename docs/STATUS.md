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

v0.12 requires a manually declared retracted pose (StickB held2seconds while disabled), captures zero once per startup, retains it through confirmed stops and Bluetooth reconnection, and blocks enable when motor/CAN reference trust is lost. R1 remains a held enable; neutral commands active position hold while held. Limits0..80deg, current1A, speed1rad/s, acceleration5rad/s^2. See MOTOR-RECOVERY.md for required startup and reference limitations. No fixed reference survives a Stick restart; no hardware home sensor exists yet.

The v0.6 log verifies both+/-20deg bench endpoints and R1-release disable. The user reports v0.9 direction, motion, controller-loss stop and reconnection worked with the leg system attached, superseding our assumption it remained unloaded. The user reports1A is sufficient to hold the lighter-spring test rig. v0.12 logs show fixed-angle operation after arming and completed cycles; startup repeatability and heavier-spring holding remain unmeasured. Host simulation covers fixed zero, held-enable stops/recovery, reference-invalid lockout and prior transport/readback guards.

The user reported REF INVALID on controller power-off in v0.10. v0.11 fixes a simulated reproduction where in-flight CAN operations aborted by controller loss were classified as CAN failure. The user subsequently confirmed that v0.11 fixed it; genuine transport failure/unconfirmed STOP still invalidates reference. The user also reports1A holds the lighter-spring test rig.

v0.12 adds a fresh controllerY-triggered one-motor retract/pause/extend/hold sequence at unchanged test limits. Endpoint arrival and continuous300ms retracted settling are required; a6second stage timeout stops, joystick overrides, and loss/stop cancels without resume. Host simulation and compilation pass. The user's v0.12 log confirms two completed cycles, retracted pause near0.09deg, extended hold at79.95–80.00deg, manual joystick override and R1-release disable during a later retraction. Moving samples show up to4.89deg target/measurement difference; higher-speed tracking and loaded launch performance are not established. See bench-logs/motor-recovery-v0.12-cycle-excerpt.txt.

## Available and planned hardware

One RobStride 05 is currently available (bench ID1); the second leg motor is planned. Spinner ESC: brushless AM32, 100 A; PWM versus DShot undecided. Left/right motor assignment is not established.

## Agreed but not implemented

A repeatable physical retracted reference/jig or home sensor (manual declaration now implemented); physical CCW extension to80degrees; standing height TBD; two coordinated leg motors; jump mode; gyro-walk mode; spinner ESC output; global loss/recovery handling across every actuator; GUI/AX12 integration; calibrated IMU control and persistent onboard logging. See CONTROL-ARCHITECTURE.md and LEG-MECHANISM.md.

## Historical material

LegacySetup/LegacyParameterRead/MotorJog and the chronological RS05-FIRMWARE.md preserve the investigation of firmware0.5.0.3. Earlier4000=200 ms statements were not established on that firmware and are superseded by the tested20,000-count setting on0.5.0.14. Older sketches are not a deployment recipe. Raw uploaded logs include earlier invalid sessions followed by observer restarts; the final clean session is identified in WATCHDOG-DISCONNECT.md.

## Validation

scripts/check.sh runs all host simulations and Python checks. scripts/build.sh uses the pinned cloud toolchain; specify the firmware environment. Host simulations and builds do not replace device acceptance tests. No new mechanism motion should use the relative bench controller as an absolute leg-limit implementation.
