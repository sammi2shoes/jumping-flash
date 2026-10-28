# Rapid SB switching / reference preservation — v0.3 + APK v0.7.2

[v0.4 / APK v0.7.3](AX12-LEG-MIXES.md) adds three SC-selectable single-stick leg mixes while retaining these reference/recovery changes.

[APK v0.7.2](../releases/JumpingFlash-AX12-v0.7.2.apk) · [Stick v0.3 ZIP](../releases/AX12DualPosition-v0.3.zip) · [app-only binary](../releases/AX12DualPosition-v0.3-app.bin)

## Evidence

The user's v0.7.1 log reported an ID1 write-verification mismatch during repeated arm/disarm attempts, followed by reference invalidation. Internal reports were clean (max55ms update gap; no≥120ms gaps, corrupt frames or rejected source updates). That log did not contain the parameter index or requested/returned bits, so it cannot establish whether the mismatch was a delayed read, parameter quantization, a real setting difference or another cause. This release fixes the overly broad reference invalidation and adds regression coverage for rapid switching; it does not claim a hardware-proven root cause.

## App-based zero capture

Flight Deck now has **Set both retracted zeros**. With SB OFF, centered fresh inputs, both motors configured/disabled and no prior captured reference, place BOTH outputs at their known fully retracted poses and confirm the dialog. The Stick sends STOP, reads both positions, verifies they stay within0.002rad over100ms, then records two independent offsets. It never enables, homes, calibrates or writes an encoder-zero command. Both offsets commit together only after all checks pass. The app waits for reference-valid telemetry; sending the request alone is not success. A2.5second acknowledgement timeout reports rejection or a mismatched firmware version.

There is no need to use Stick B. B remains an optional fallback. Existing captured zeros, recovery-pending references, and hard-invalid captured references cannot be overwritten during that Stick boot. Hard loss of pose trust still requires a known-pose restart; the app cannot infer retraction. No late/background automatic zero capture or retries are added.

## Changes

- **Arm input must remain stable for200ms. Disarm is immediate.** The window applies only before enabling, not to ongoing motion commands. App arm confirmation allows up to1second for this window plus bounded setup; it still times out if RUN is not confirmed. Input/telemetry freshness deadlines are unchanged.
- The BLE receiver retains an internal disarm-edge counter. An OFF→ON received between motor-loop services cannot disappear just because the latest snapshot says ON. It cancels a pending arm or active control; both motors stop. No protocol-frame change is needed for this counter.
- State commands wait for a fresh reply in the expected state within their existing100ms bound. An old RUN reply is not accepted as STOP acknowledgement, nor does it immediately fail a STOP that is still awaiting its real reply. Fault frames still stop processing.
- A mismatched write readback gets one **confirmation READ**, without replaying its write or widening tolerance. Cancellation is checked before interpreting the result. If mismatch persists, stop and validate through the existing communication-recovery path. An unverified target write alone does not prove the mechanical zero changed. Serial now includes ID, parameter index, wanted and actual bits.
- A missing settings reply is treated as a communication failure, not unconditionally escalated into a hard reference fault. A correctly received value showing changed mode/current/timeout/speed still invalidates trust. An unexpected RUN/STOP mode likewise requires STOP and validation; it does not by itself prove encoder reset.
- Telemetry flag bit7 indicates saved references are **pending recovery validation**. The app displays **CAN RECOVERY — SB OFF** and tells the operator not to re-zero. Keep SB disarmed and controls centered while original settings and encoder continuity are checked. Success restores ready/reference-valid status without changing origins; arm deliberately afterward. A detected restart, nonfinite/jumping/out-of-range encoder, actual setting change, or motor fault still blocks reference trust.

Current/speed/travel remain1A/1rad/s/fixed0–80°. Background watchdog configuration remains automatic; the optional energizing diagnostic is not part of normal startup or recovery. No gait/jump/spinner outputs or automatic re-enable are added.

## Install and check

Install APK over the current version. Both motor supplies OFF while flashing. Extract the ZIP, close Serial Monitor, and run from that folder using the user's Stick port **COM20**:

```powershell
py -3.13 -m esptool --chip esp32 --port COM20 --baud 460800 write_flash 0x10000 AX12DualPosition-v0.3-app.bin
```

This is an app-only update at0x10000 for the established partition layout; do not erase NVS. Flash/reset loses boot-local references, so declare both known retracted poses once using **Set both retracted zeros** in Flight Deck. Confirm the physical poses in the dialog; wait for POSITION CONTROL READY. Start acceptance with bare outputs, SB off and centered controls. Automatic motor setup needs no watchdog button.

After normal small-motion/neutral-hold acceptance, check arm→disarm and repeated short arm requests while centered. Requests shorter than200ms should not energize either motor; disarm should stop immediately. An interrupted arm should preserve trust if STOP is confirmed and no independent fault occurred. If recovery is pending, keep SB off/controls centered and wait for ready; do not reset/re-zero merely because trust is temporarily suspended. Export any fault history and capture serial wanted/actual/index lines if the issue recurs. Actual faults may still require a known-pose restart; reference preservation is conditional, not guaranteed across arbitrary resets or slip.

## Validation

Firmware compile, full host suites, Android build/lint and75 Android tests pass. New motor simulation covers50 short SB pulse pairs with no enable; OFF/ON during a first-motor enable stopping both without losing zero; stale RUN before a delayed STOP acknowledgement; one mismatched target read confirmed successfully; persistent mismatch stopping then recovering reference after validation; missing settings replies recovering without recapture; and existing hard reset/settings/encoder guards. The three-minute dropped-reply simulation still passes. BLE receiver tests verify disarm-edge evidence survives an OFF/ON between motor-loop services. Android tests verify bounded arm-confirmation allowance, fault history, and the recovery-pending message followed by ready without automatic arm. Remote-reference tests cover opt-in/disarmed/neutral/session/replay guards, capture without any Stick button or enable, moving-output rejection, no overwrite, app preconditions/cancellation and telemetry acknowledgement. Physical v0.3 acceptance is pending.
