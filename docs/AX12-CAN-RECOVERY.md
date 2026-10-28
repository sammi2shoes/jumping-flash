# Automatic watchdog setup and bounded CAN recovery

**Historical v0.2 procedure.** [v0.3 / APK v0.7.2](AX12-SB-RECOVERY.md) adds rapid-SB handling, confirmation reads and a distinct recovery-pending reference state.

Matched release: **AX12DualPosition v0.2 / APK v0.7.1**. Same RS05 CAN2.0 firmware0.5.0.14, right ID1 negative extension / left ID2 positive extension. Still the1A,1rad/s, fixed0–80° position bench. No new gait, jump or spinner behavior.

- [APK v0.7.1](../releases/JumpingFlash-AX12-v0.7.1.apk)
- [Stick source/binary ZIP v0.2](../releases/AX12DualPosition-v0.2.zip)
- [App-only binary](../releases/AX12DualPosition-v0.2-app.bin)

## Normal operation: no watchdog button

With SB disarmed, centered controls and a fresh AX12 link, the Stick automatically configures each motor's timeout20000counts (manual:1second), position mode5,1A current and1rad/s speed, and reads the settings back. It never enables a motor during automatic configuration. The dual-position app removes the required watchdog-button workflow and displays **AUTO CONFIG VERIFIED**. This is configuration verification, not a fresh behavioral A/B result. Both motors' timeout action was previously observed on this firmware; no new physical validation is claimed for this release.

The energizing, timeout0-versus20000 behavioral test remains an optional serial `t` diagnostic for a bare-output bench, not a startup or recovery task. It is never run automatically. A failed explicit behavioral test still blocks operation. Manual known-retracted reference capture with Stick B is still needed once per Stick boot; watchdog automation does not infer mechanical pose.

## Fault policy

| Event | Response |
| --- | --- |
| Isolated missing critical read reply | Up to3 READ requests, each30ms reply wait plus bounded TX/service overhead. Hold the last bounded target while waiting; no stop, re-enable, re-zero or watchdog menu if a retry succeeds. |
| Corrupted CAN frame | Normal CAN hardware validation rejects it. READ frames allow hardware retransmission; actuator/enable frames remain single-shot to avoid stale command replay. |
| Missing/rejected/nonfinite voltage telemetry | Keep motor control if critical feedback is healthy. No whole-controller reset for this auxiliary measurement. Age out the displayed lower-pack voltage after3seconds without fresh readings from both packs. CAN controller/TX failures themselves remain faults. |
| All critical read retries fail, CAN TX deadline/failure, or bus-off | Request STOP on BOTH; use the independent motor timeout as fallback if CAN is unavailable. Suspend reference trust but keep candidate origins. No stale motion replay or automatic enable. |
| Communication returns | With SB disarmed and centered/fresh inputs, recover the CAN driver and confirm STOP. Check both motors' original mode/current/timeout/speed before rewriting anything, plus finite positions inside the fixed range (±0.002rad arming tolerance) and displacement≤0.3rad from the last trusted sample. Only then restore reference validity and automatic configuration. A deliberate SB off/on cycle is still required for movement. BLE need not be disconnected. |
| Settings changed, explicit parameter rejection/write mismatch, nonfinite/out-of-range/discontinuous position, restart/identification after capture, or motor-reported fault | Stop BOTH and invalidate reference trust. Do not silently clear faults, overwrite zero, or auto-enable. A known-pose restart/reference procedure is required. |
| Following error>0.15rad | Stop BOTH. Confirmed STOP preserves trusted origins; investigate load/mechanics/current before deliberately rearming. |
| SB off, operator stop, Bluetooth/input staleness | Stop BOTH; confirmed STOP preserves trusted origins. Existing fresh-input and deliberate-rearm rules remain. |

The retry window is not a guarantee of end-to-end latency, and the1second motor watchdog is not a promised loaded stopping time. Hardware bus-off recovery can take longer than a brief read retry. Any missed STOP confirmation means torque removal has not been confirmed; serial reports that explicitly.

Recovery cannot prove absolute mechanical pose after every possible undetected reset or slip. Identification broadcasts conservatively invalidate trust even if caused by another CAN master requesting identification. Do not run MotorStudio/other command senders on the active control bus. Physical referencing remains necessary for the competition mechanism.

## Better diagnostics

Dual-position telemetry byte14 now packs motor ID in the upper nibble and diagnostic reason in the lower nibble while faulted. New app versions decode it; older versions still reject nonzero fault status. Reasons:1CAN I/O,2read timeout,3parameter rejection,4write verification,5nonfinite encoder,6range,7continuity,8settings,9unexpected mode,10motor fault,11restart/identification,12following error,13STOP unconfirmed. Soft-stop reasons are printed serially; the fault byte is nonzero only while the controller is uncertain. The app retains up to16 observed fault events independently of scrolling input logs. These are timestamped historical observations, not current freshness claims.

Serial also reports motor/index/retry count, reference-recovery eligibility and the specific diagnostic. Communication errors no longer share a message with invalid encoder values.

## Install and initial check

Keep both motor supplies OFF while flashing. Install APK over the current app; extract the ZIP and use the Stick USB port in place of COM20:

```powershell
py -3.13 -m esptool --chip esp32 --port COM20 --baud 460800 write_flash 0x10000 AX12DualPosition-v0.2-app.bin
```

App-only offset0x10000, not0x0. Partition compatibility is checked against the existing SingleLeg/DirectionTest layout; do not erase NVS. Keep outputs bare for initial acceptance of this recovery release. Power motors and connect with SB off/controls centered. Automatic configuration should finish without a watchdog button. Mark both mock retracted poses, release/hold Stick B2seconds, and check POSITION CONTROL READY. Begin with small travel and neutral hold. Do not deliberately break a powered CAN connector during motion to test retries; controlled electrical fault injection and loaded acceptance remain separate work.

## Verification

Plus2 firmware compile, full host suites, Android build/lint and71 Android tests pass. Motor simulation covers auto configuration without any enable; one/two missing read replies while armed; optional voltage failures; three-minute motion with18 dropped replies and no stop/re-enable/configuration cycle; longer communication loss and actual simulated bus-off followed by validated reference restoration with no automatic enable; changed settings, restart and encoder faults refusing recovery; optional A/B failure; and existing limits/hold/stop behavior. Tests do not emulate CAN electrical noise or real motor firmware. Physical acceptance of v0.2/v0.7.1 is pending.

## JumpRopeStick comparison

Reviewed [amcchord/JumpRopeStick](https://github.com/amcchord/JumpRopeStick) at commit **2e916574c885e0a7f2b75004d01ce229965df853**. This may differ from the version the user drove. Relevant source:

- [MotorManager::poll / sendMessage / checkStaleness](https://github.com/amcchord/JumpRopeStick/blob/2e916574c885e0a7f2b75004d01ce229965df853/main/motor_manager.cpp): nonblocking receive; a200ms parameter timeout clears pending state rather than resetting the whole controller; ordinary CAN transmission (single-shot flag not set); status polling and staleness tracking.
- [config.h](https://github.com/amcchord/JumpRopeStick/blob/2e916574c885e0a7f2b75004d01ce229965df853/main/config.h): one motor status ping per100ms, motor stale at2000ms and removed after10000ms.
- [ensureMotorReady / initMotorForTrim](https://github.com/amcchord/JumpRopeStick/blob/2e916574c885e0a7f2b75004d01ce229965df853/main/sketch.cpp): skip stale motors; reinitialize a non-running motor when the arm-control path asks for readiness; avoid repeating its first-connect mechanical zero via a boot-local tracker. That path clears motor faults and enables after configuration without this project's checked fixed-reference recovery.

No explicit `twai_initiate_recovery` call was found in its `main` source at the reviewed commit. Source review does not establish the cause of the user's robust driving experience. Useful principles adopted here are tolerance for an isolated missing reply, separation of auxiliary telemetry from control health, normal hardware retransmission for harmless reads, and automatic setup. RS02 specifications, its mechanical-zero/fault-clear behavior, and its zero-filled motion/status-poll frame are not copied into the RS05 controller. We retain checked RS05 parameters and the fixed-origin recovery conditions above.
