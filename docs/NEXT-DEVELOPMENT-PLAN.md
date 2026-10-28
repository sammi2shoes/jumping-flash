# Next development plan: interface, test power, sequencing, latency and IMU

This is a design proposal following the user's SC-middle test. The user reported that the leg connections failed mechanically while the control looked promising and is repairing them. Treat that run as inconclusive for loaded motion performance; do not classify the mechanical failure as a proven firmware fault or as successful loaded acceptance. Current published baseline remains APK0.7.3 / AX12DualPosition0.4. This document changes no actuator limits or runtime behavior. Stick USB port remains COM20.

## 1. A fixed-screen interface

Requirement: **no scrolling on any app page**, including tools/setup. Use the AX12's landscape screen with a permanent status/STOP header and bounded pages, not a long settings document in a ScrollView. Preserve the Jumping Flash–inspired sky/navy/yellow visual language, but prioritize leg state and controls over decorative space.

Proposed four primary pages:

| Page | Screen content |
| --- | --- |
| Flight Deck | Two leg drawings/gauges with measured angle, target and limit markers; selected SC mix; live CH1/CH2 indicator; arm/reference/connection state; one contextual setup action. |
| Inputs | Gimbals, SB/SC/SD, throttle and dial values. Raw42-source inspection is paginated, never appended below the controls. |
| Robot | Standing-height/sequence preview, selected test-power profile and IMU orientation/quality. Separate subpages for editing; no edits to actuator profiles while armed. |
| Tools | Fixed tiles for Connect, Reference, CAN, IMU, Logs and Settings. Each opens a bounded screen with Back/Next or tabs. Long logs are exported, not rendered as an endless wall. |

Keep STOP visible on every screen, and distinguish measured travel from target/preview. Show stale/unreferenced data as unavailable rather than animate a confident pose. A later CAD view should reuse the [commanded versus measured mapping roadmap](CAD-INPUT-MAPPING-ROADMAP.md), not imply that motor angle completely determines the spring-loaded ankle pose. Start with a leg schematic; load a simplified CAD representation after axes/pivots are defined.

Implementation acceptance: layout tests at the actual AX12 logical window size/density and relevant font scales, all actions reachable with zero vertical/horizontal scroll, no clipped status/STOP, and hardware review. Use smaller information groups and separate pages rather than shrinking all text to force a fit. UI redraw should stay independent of command transmission. Android system overlays/background handling retain the existing stop/rearm behavior.

## 2. Increase test power without mixing variables

The current build limits each motor to1A and position target/speed to1rad/s. An80° stroke at1rad/s takes about1.40seconds even with instantaneous acceleration and no load.48V cannot override that software speed ceiling. More current can improve available torque/tracking; it does not remove the speed cap. Bus voltage, motor phase/current-limit parameters, battery current and actual joint torque are different measurements.

Replace compile-time-only tuning with named, bounded profiles, selected while SB is disarmed. An example progression to evaluate after mechanical repair is:

| Profile | Candidate current / velocity | Purpose |
| --- | --- | --- |
| Baseline | Existing1A /1rad/s | Known behavior, direction/reference/hold checks. |
| Stronger tracking |2A /1rad/s | Change torque allowance while keeping the same motion timing. |
| Faster stroke | Start from an accepted current profile, then test2rad/s | Isolate speed and tracking/overshoot changes. |
| Launch development | Separate profile after measured loaded strokes | Derive actual motor/ESC/battery/structure limits and cutoff/reversal margins before choosing numbers. |

These are proposed experiments, not newly applied limits or a claim that a candidate is appropriate for the repaired prototype. Verify RS05 parameter units/ranges against its own documentation and firmware, not RS01/02 defaults. Record each profile in the log and read back settings before arm. Change one major variable per comparison. Do not send an unlimited/sentinel current value; eventual full-output operation still means explicit verified hardware limits.

Separate armed neutral-hold demand from stroke demand if measurements justify it. Until then keep the existing profile rather than invent a holding-current value that cannot support the ankle spring. SB off still disables torque; active hold is not passive support. Capture target/measured position, velocity, current where supported, bus voltage, faults and timing. A current limit alone is not an energy limit: launch also depends on torque over joint angle, linkage geometry, spring energy, losses, contact and load. Address failed joint retention before increasing load.

## 3. Local sequence engine, before a walking controller

The AX12 should send intent: desired travel/turn, mode, height, explicit one-shot events and limits. The Stick should own a deterministic sequence clock and both leg trajectories. Do not schedule individual gait steps with Android timers or Bluetooth round trips.

First implement a **sequence preview** with no motor output, followed by low-amplitude physical alternation after mechanical acceptance. Parameters include standing/base angle, amplitude, period, phase offset, dwell and per-leg offset. The existing mixes remain the manual fallback. Determine standing angle from linkage/load testing; do not assume40° is mechanically correct simply because it is the midpoint.

A simple motion-only bench waveform is qL=base+A sin(phase), qR=base−A sin(phase), with bounds enforced independently. This demonstrates synchronization and phase control; it is not proof of walking, foot contact or gyro-propelled translation. Smooth trajectories should obey explicit velocity/acceleration bounds and should be generated from elapsed time rather than accumulating timing error from delayed loops.

The gait state machine should distinguish idle/hold, weight transfer, unload/lift, place/recover and opposite-side transfer. Operator forward input can set cycle rate/amplitude; turning can bias the two sides after observations establish a useful mapping. Cancelling a sequence returns to a defined bounded manual/hold state, and stop/loss of command discards phase/event requests. Never resume a stored jump automatically after recovery.

Gyro walking needs spinner direction/speed, body axes, contact and actual response. Rotor angular momentum is I×omega and gyroscopic torque follows the cross product with the imposed body rotation; leg alternation alone does not establish forward motion or the correct sign. Throttle is not a measured rotor speed. Spinner RPM feedback and ESC interface remain future integration work. Develop the leg sequence without spinner actuation first, then characterize the combined system separately.

SC currently selects three manual mixes. Do not silently replace those with gait modes; retain a clearly named bench/manual configuration and explicitly define a future operator mode set. Jump remains a separate one-shot trajectory, not just an extremely fast sinusoidal gait.

## 4. Reduce and measure latency

Observed code paths in the current baseline:

- AX12 internal source updates are approximately25Hz in the submitted logs, about40ms between accepted updates. That cadence is a source-stage observation, not a measured stick-to-torque latency.
- The app attempts commands about every20ms from a10ms UI Handler pump; rendering and input logging also run there.
- Dual-position target updates are gated every50ms (20Hz), and position/setting/target verification reads are synchronous. Read retries and optional work can add delay.
- BLE scheduling, firmware service work and motor response add their own delay. Adding nominal40/20/50ms stages is not an exact or guaranteed worst-case end-to-end measurement.

Measure timestamps for input report received, command built/queued, Android BLE write callback, Stick receipt, target transmission and first measurable motor response. Phone and Stick clocks must be aligned/calibrated if subtracting across devices; a BLE write callback alone is not motor execution. Report median,95th/99th percentile, maximum and jitter under logging/UI load, not only an average. Use a physical trigger/video or external timestamp reference to validate end-to-end measurements when available.

Proposed architecture:

1. Consume validated input on a dedicated worker. Send fresh changed intent promptly, with a separate heartbeat deadline. Maintain one in-flight BLE write and keep only the newest unsent intent; never queue old joystick demands. Disarm/stop supersedes motion.
2. Keep Android rendering around10–20Hz and move log formatting/export off the command path. Lower rendering rate must not lower command rate.
3. Negotiate a shorter BLE connection interval/high-priority connection and measure the actual result. A request is not proof that the negotiated interval changed. Do not tighten freshness limits solely to fit a desired latency number.
4. Replace blocking per-target verification with bounded asynchronous CAN transactions, current feedback and a deterministic local control tick. Candidate loop rates are100–200Hz, subject to measured motor feedback rate and CAN utilization. Put configuration readback before arm; reduce ordinary battery/settings polls outside the critical stroke. Retain local bounds, reset/reference detection, stale-input handling and watchdog fallback.
5. Keep command/control and deferred display/logging separate on the ESP32. Use short memory snapshots/ring buffers with explicit loss counters rather than serial printing or allocation in the critical update. A small pre/post-event trace is more practical initially than streaming every IMU sample through the existing20byte telemetry channel.

The 200ms stable-arm window is a pre-enable qualification; it does not need to be removed to reduce latency while already armed. After Android/CAN improvements, the native report cadence may dominate. Investigate a verified faster internal stream only with protocol evidence; do not replay unknown radio/model/RF configuration packets.

## 5. IMU integration in stages

The Plus2's MPU6886 has already supplied data in the diagnostic sketch; the dual-position firmware does not currently use it for motion control. Bring it into this architecture in three stages:

**Instrumentation first:** define body forward/left/up and the sensor-to-body transform; verify signs by deliberate rotations; calibrate stationary gyro bias; verify configured sample rate and acceleration/gyro ranges against expected vibration/impacts. Timestamp samples on the Stick, count misses/clipping, and log the orientation quality/state. Candidate sampling100–200Hz depends on measured I2C/control timing.

**Attitude estimate and display:** fuse gyro with accelerometer correction when acceleration is suitable. A six-axis IMU has no absolute magnetic yaw reference, so yaw drifts. During a launch, free fall, impact or strong vibration, acceleration is not a clean gravity vector; reject/downweight inappropriate correction and expose poor-quality estimates. Do not interpret an acceleration spike as proven foot contact.

**Bounded feedback:** after observing real data, add small roll/pitch corrections to accepted stance/sequence trajectories, respecting each leg's limits and current profile. Verify sign, delay and saturation before increasing gain. Keep joint-angle control on motor encoders; the IMU does not replace their fixed zero references. Airborne spinner reaction-torque control comes later, with measured inertia, RPM/control response and flight-state evidence rather than an untested attitude estimate.

## Proposed order and acceptance gates

1. Repair/verify leg retention and charged-battery baseline; retain the current published build for comparison.
2. Build the no-scroll layout and latency instrumentation while the rig is unavailable; keep actuator behavior unchanged for that UI comparison.
3. Integrate passive IMU sensing and the asynchronous control/feedback architecture, measuring timing before/after.
4. Introduce a single stronger-tracking profile after baseline mechanics pass, then isolate faster-stroke tests. Keep a quick baseline profile selection while disarmed.
5. Add motor-free sequence preview, then local low-amplitude alternating motion and data review. Progress to gyro-assisted walking and launch profiles with actual loaded/contact/spinner measurements.

This is a proposed roadmap, not completion or physical validation of any of these features. The user asked to think through the next stage while repairing hardware; no power, failsafe, gait, IMU or UI runtime behavior is changed by this planning commit.
