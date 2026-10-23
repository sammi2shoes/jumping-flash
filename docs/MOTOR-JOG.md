> **Historical document.** This records an earlier project stage and includes superseded firmware/timeout information. Use the root README and docs/STATUS.md for current hardware, validated results, and supported workflows.

# One unloaded motor jog

Use only the user's already validated M5StickC Plus2 / Mini CAN / RS05 CAN 2.0 wiring, one unloaded motor ID 1 with no spring-loaded linkage attached, and independent motor power disconnect. Secure the motor body and keep the shaft clear. This deliberately replaces the IMU/Bluetooth sketch; no radio input is used.

Open firmware/MotorJog/MotorJog.ino as a new Arduino IDE sketch, using the existing working core, libraries and board settings. Upload with motor power off, power the motor, then restart the Stick so its initial stop can be acknowledged. Serial Monitor: 115200 baud. Boot sends stop commands but no enable or movement command. Send `g` for one jog; send `x` or press button A to request stop. Each later jog requires a new `g` and uses a fresh measured reference. New g commands during a test are ignored. Independently disconnect motor power if stopping is not confirmed or behavior differs from the command.

The test first obtains stop/reset feedback, writes/reads back velocity mode 2 (0x7005), current limit 0.30 A (0x7018), CAN watchdog 4000 ticks = 200 ms (0x7028), and zero speed target (0x700A). All prerequisites must succeed before enable. No settings are saved to motor flash, and the test does not restore old settings automatically; the next control firmware must configure its own settings. A motor firmware reset may change them. A rejected watchdog/current setting aborts rather than relaxing it.

After a fresh finite position read (0x7019), enable must produce run-mode status feedback before commanding +0.10 rad/s. The host commands that speed for one second, approximately +0.10 rad / 5.7 degrees if followed. Actual travel is measured, not assumed. Position is polled every 50 ms; stale/missing/invalid feedback aborts, and a measured displacement greater than 0.15 rad (8.6 degrees) aborts. The one-second host duration is checked after synchronous reads, but stop/communication and mechanical response still have latency: these are software bounds, not guarantees of exact physical travel or stopping distance. Feedback fault flags also abort. Completion sends up to three stop frames and requires reset/disabled feedback. Completion does not establish that the requested travel was achieved; inspect the reported delta.

The motor's own communication watchdog provides a device-side stop if host transmissions cease, subject to its firmware implementation. Readback verifies the configured value, not actual power-failure/disconnect behavior. CAN stop and its acknowledgements depend on a working bus; the separate power disconnect remains necessary for communication faults. No fault-clearing, zero-position-setting, automatic retry/re-enable, gain increase, or persistent save is implemented. A low current cap may be insufficient to move: report the logs rather than increasing it automatically.

Sources: official RS05 CAN 2.0 manual sections 3.3.6, 4.1.4–4.1.5, 4.1.7–4.1.8, 4.1.14 and 4.3.3; official RobStride Python SDK confirms mode changes while disabled. Register 0x7028 documents 20000 ticks as one second. The standard position/velocity/voltage reads have passed on the user's motor. Current-limit, watchdog, mode-write acceptance and physical jogging remain on-device acceptance checks.

Cloud build:

```sh
scripts/build.sh -e motor-jog
```

Simulated checks compile the actual sketch with fake Arduino/CAN interfaces:

```sh
g++ -std=c++17 -Itests/jog-host tests/jog-host/test.cpp -o /tmp/jumping-flash-jog-test
/tmp/jumping-flash-jog-test
```

These exercise a single successful jog, verified prerequisites, rejected current/watchdog settings, invalid starting position, absent stop acknowledgement, serial abort, missing motion feedback, and excess travel. They are not physical motor validation.

## On-device watchdog rejection

The user supplied logs showing stop/reset feedback, accepted mode and current-limit readbacks, then `READ FAILED index=0x7028 response=1 accepted=0 aborted=0`. This proves a rejected watchdog read, not a timeout. It does not prove whether the preceding write succeeded. The jog aborted before enable and received stop confirmation. The documentation lists an upper-computer CAN_TIMEOUT index 0x200C as well; its accessibility on this firmware is unverified. Do not replace 0x7028 with 0x200C for writes without first testing read support.

`firmware/WatchdogDiagnostic/WatchdogDiagnostic.ino` (target watchdog-diagnostic) sends a stop, the documented version-read request (type 4, bytes 00 C4), and read-only type-17 probes of 0x7028 and 0x200C. It prints all received frames and actual read status codes. It sends no type-3 enable or type-18 parameter-write frames. With motor powered, restart the Stick and capture serial output at 115200 baud; it runs once after three seconds. This diagnostic does not prove the motor watchdog operates.

## Official firmware release investigation

The manufacturer release notes identify expanded type-17/type-18 parameter access in RS05 **0.5.0.5**, newer than this motor's **0.5.0.3**. The inspected official release provides **0.5.0.14**. See [RS05-FIRMWARE.md](RS05-FIRMWARE.md) for exact evidence, update links, adapter requirements and post-update checks. No motor firmware has been updated; the watchdog prerequisite remains enforced.
