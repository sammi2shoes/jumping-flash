# Robot startup and operating-mode requirements

These are agreed requirements, not a claim that the complete robot controller is implemented.

## Hardware and reference

- M5StickC Plus2 and MiniCAN. One RobStride 05 leg motor is currently available; two motors are planned for the finished robot. Spinner ESC will be powered with the robot.
- Operator powers the Bluetooth controller first, then powers the robot with restrictive leg safety locks installed.
- After valid/stable disabled motor feedback is available, record each locked maximum-retracted pose as that leg's session 0 degrees. This is a software offset, not an encoder factory calibration or persistent motor-zero write.
- Counterclockwise powered-joint travel extends the leg from0 to 80 degrees, with a user-provided margin before the mechanism's physical maximum. Viewing side and the encoder sign must be verified per leg, particularly for mirrored assembly.
- Standing angle remains to be established through testing.
- The current bench motor is CAN ID1. Left/right assignment and the future second motor ID are undecided. The spinner ESC is a brushless AM32 100 A unit; PWM or DShot is undecided. The exact ESC hardware model/pinout and motor characteristics are not specified. The ESC100 A rating is not a validated system current limit.

## Startup phases

1. Boot with motors and ESC disarmed. Wait for motor readiness; capture the locked references while motor feedback is stable and disabled. No automatic leg movement or spinner start.
2. Establish fresh Bluetooth controller data. Pairing does not itself enable outputs.
3. Operator removes all mechanical/weapon locks. Without lock sensors, software cannot infer this from Bluetooth connectivity or elapsed time.
4. Arena is closed; operator explicitly arms using a deliberate controller action. The release/press enable sequence is the agreed recovery pattern. The exact lock-removal/arming input mapping still needs to be defined.
5. Before motor enable, check calibrated travel, reapply and verify limits/watchdog, and preload each current measured position rather than a cached target.

## Initial modes

- Jump: a single deliberate trigger requests both legs to retract smoothly to0degrees, then extend together toward80degrees using validated launch speed, acceleration and current limits. “Maximum speed and power” is the desired launch behavior; actual bounded values, synchronization tolerance, retract-complete conditions and abort behavior are not yet established. No repeated jumping just because a trigger remains held or a connection returns.
- Gyro walk: forward input requests an alternating leg cycle that lifts one side at a time, coordinated with the vertical spinner's gyroscopic response. Standing angle, lift amplitude, phase/timing, spinner speed/direction and IMU use require model review and controlled testing; alternation alone does not establish the resulting travel direction.
- Additional modes can be added behind the same global arming/failsafe gate. Mode-selection controls are not assigned yet. Pending mode changes must not unexpectedly launch a stored movement sequence.

## Loss and recovery

- Bluetooth disconnect or stale input requests stop of BOTH legs and the spinner ESC and cancels current gait/jump commands. This is an actuator-output failsafe; IMU sensing and logging can continue.
- CAN/critical-motor failure also cancels coordinated leg operation. Motor watchdog provides fallback when the Stick cannot deliver a stop.
- Return of communication restores a disabled ready state. Verify health/settings and current positions, require fresh neutral input and release/press enable, then accept new commands. Do not resume a stored jump, gait phase, or spinner demand.
- Preserve original calibrated zero across ordinary CAN reconnection. Motor reset/power-cycle or an uncertain encoder reference invalidates it and requires the locked referencing sequence again.
- A disabled motor is not an active brake. The spring-loaded feet may release stored mechanical energy after torque is removed. Loaded behavior and the appropriate mechanical failsafe remain unvalidated.
- Spinner ESC stop behavior, reconnect/re-arm behavior, and exact competition-rule compliance cannot be claimed until its interface and the applicable rule text are checked.

## Current implementation boundary

MotorRecovery is a single unloaded-motor +/-5degree bench controller with Bluetooth freshness, explicit stop, watchdog configuration and release/press re-arm. Its origin is relative to each bench arm, so it must NOT be used as calibrated0–80 degree linkage firmware. Two-leg operation, automatic locked-zero capture, lock-removal confirmation, spinner control, jump and gyro-walk modes are not implemented yet. New code work is paused while this repository checkpoint is audited and saved.
