# Robot startup and operating-mode requirements

These are agreed requirements, not a claim that the complete robot controller is implemented.

## Hardware and reference

- M5StickC Plus2 and MiniCAN. One RobStride 05 leg motor is currently available; two motors are planned for the finished robot. Spinner ESC will be powered with the robot.
- Target competition startup: operator powers the Bluetooth controller first, then powers the robot with restrictive leg safety locks installed. The current physical prototype has no locks; it uses a manually positioned retracted pose declared with StickB held2seconds while disabled. Software can check stillness, not whether the declared physical pose is correct.
- After valid/stable disabled motor feedback is available, record each verified maximum-retracted pose as that leg's session 0 degrees exactly once per startup. This is a software offset, not an encoder factory calibration or persistent motor-zero write.
- Counterclockwise powered-joint travel extends the leg from0 to 80 degrees, with a user-provided margin before the mechanism's physical maximum. Viewing side and the encoder sign must be verified per leg, particularly for mirrored assembly.
- Zero must never be redefined by arming, stopping or reconnection. The prototype retains the numeric zero but blocks motion after motor/CAN uncertainty; known reset/power-cycle requires reference recovery. No hardware home sensor currently guarantees physical reference across all resets.
- Standing angle remains to be established through testing.
- The current bench motor is CAN ID1. Left/right assignment and the future second motor ID are undecided. The spinner ESC is a brushless AM32 100 A unit; PWM or DShot is undecided. The exact ESC hardware model/pinout and motor characteristics are not specified. The ESC100 A rating is not a validated system current limit.

## Startup phases

1. Boot with motors and ESC disarmed. Wait for motor readiness; establish the reference at the known retracted pose while feedback is stable and disabled. Competition locks are planned; the current prototype instead requires a deliberate manual reference after Bluetooth is ready. No automatic leg movement or spinner start.
2. Establish fresh Bluetooth controller data. Pairing does not itself enable outputs.
3. In the locked competition configuration, operator removes all mechanical/weapon locks. Without lock sensors, software cannot infer this from Bluetooth connectivity or elapsed time.
4. Arena is closed; operator explicitly arms using a deliberate controller action. The release/press enable sequence is the agreed recovery pattern. The exact lock-removal/arming input mapping still needs to be defined.
5. Before motor enable, check calibrated travel, reapply and verify limits/watchdog, and preload each current measured position rather than a cached target.

## Initial modes

- Jump: a single deliberate trigger requests both legs to retract smoothly to0degrees, then extend together toward80degrees using validated launch speed, acceleration and current limits. The user wants immediate leg recovery/retraction after the push, without an extended-position dwell. Reversing leg velocity still requires opposing torque; recovery timing must account for whether the foot remains loaded so it does not subtract from launch impulse. The user accepts passive coast/impact at the physical stop but does not want outward motor drive into it. The80degree software limit stays inside the hard stop; outward-drive cutoff/reversal margin must account for measured latency, speed and inertia. An extended powered hold is not the desired final jump behavior. “Maximum speed and power” is the desired launch behavior; actual bounded values, synchronization tolerance, retract-complete conditions and abort behavior are not yet established. No repeated jumping just because a trigger remains held or a connection returns.
- Gyro walk: forward input requests an alternating leg cycle that lifts one side at a time, coordinated with the vertical spinner's gyroscopic response. Standing angle, lift amplitude, phase/timing, spinner speed/direction and IMU use require model review and controlled testing; alternation alone does not establish the resulting travel direction.
- Additional modes can be added behind the same global arming/failsafe gate. Mode-selection controls are not assigned yet. Pending mode changes must not unexpectedly launch a stored movement sequence.

## Loss and recovery

- Bluetooth disconnect or stale input requests stop of BOTH legs and the spinner ESC and cancels current gait/jump commands. This is an actuator-output failsafe; IMU sensing and logging can continue.
- CAN/critical-motor failure also cancels coordinated leg operation. Motor watchdog provides fallback when the Stick cannot deliver a stop.
- Return of communication restores a disabled ready state. Verify health/settings and current positions, require fresh neutral input and release/press enable, then accept new commands. Do not resume a stored jump, gait phase, or spinner demand.
- Preserve original calibrated zero across ordinary CAN reconnection. Motor reset/power-cycle or an uncertain encoder reference invalidates it and requires the locked referencing sequence again.
- A disabled motor is not an active brake. The spring-loaded feet may release stored mechanical energy after torque is removed. Loaded behavior and the appropriate mechanical failsafe remain unvalidated.
- Spinner ESC stop behavior, reconnect/re-arm behavior, and exact competition-rule compliance cannot be claimed until its interface and the applicable rule text are checked.

## Competition latency priority

The user explicitly prioritizes minimum control/launch latency over logging, telemetry reads and repeated checks, including sacrificing those activities when necessary. This is a competition design requirement; current diagnostic bench firmware is not the final competition timing architecture.

- Give the launch/setpoint/cutoff path a predictable high-priority schedule. Choose its rate from actual motor-response/CAN-bus timing and worst-case measured execution time; do not promise a rate from nominal loop delays alone.
- Verify motor configuration, current/speed ceilings, watchdog and reference before arming/starting the stroke. No blocking parameter readback or write-and-verify transactions in the time-critical launch interval. Preconfigure limits/mode so an intentional profile change cannot stall cutoff handling.
- Consume already-received motor state through a bounded receive/cache path. Run bounds, cached fault flags, controller freshness and emergency-stop handling inline without waiting for optional telemetry replies. Outward cutoff/recovery commands take priority over optional work.
- Buffer only inexpensive snapshots when timing budget permits. Defer formatting, serial/WiFi output, display work, optional IMU diagnostics and analysis until outside the critical stroke, or omit them. IMU data actually needed for control is a control input, not optional logging.
- Armed CAN transmission belongs to the control path. A background logger must not send reads/heartbeats that keep the motor watchdog alive after the control task has stalled. Background diagnostics are receive-only or explicitly gated off while armed.
- Preserve fixed-zero integrity, local command bounds and watchdog/stop behavior through the low-latency architecture. Loss of reference blocks output; a timing optimization must not silently re-zero or resume a cancelled launch. Stale-input/fault handling must not wait behind a logger.
- Measure command-to-motor action and cutoff/reversal timing under the real load. The v0.15 free-swing trace sampled10ms control intervals and one19ms interval; at5rad/s,10ms corresponds to2.86degrees, and at480rpm unloaded it corresponds to28.8degrees. These are distance-per-delay calculations, not proven safe margins or exact command-to-torque measurements. A few-degree cutoff gap is not yet validated at maximum motor speed.

## Current implementation boundary

MotorRecovery v0.15 is a one-motor prototype with a manually declared zero, fixed0..80degree travel, held R1, Y slow cycle, and X slow-retract/faster-extend trial. X requests3A/5rad/s extension and40rad/s^2 target acceleration, with nominal10ms fast-stage control updates, an endpoint settling hold, verified low-limit restoration and a1second software burst deadline. It still performs synchronous parameter reads/write verification and serial/display work; it therefore does not implement the competition latency requirement or the requested immediate-recovery jump trajectory.

The v0.15 log confirms profile switching and514ms reported endpoint arrival while the leg moved freely/off the ground; it does not establish weight-bearing launch performance. Two-leg operation, automatic locked-zero capture, lock-removal confirmation, spinner output, a full-force immediate-recovery jump, gyro walking, and guaranteed reference recovery across arbitrary resets remain unimplemented. See MOTOR-RECOVERY.md and STATUS.md for the current tested baseline.
