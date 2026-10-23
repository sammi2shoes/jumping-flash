# RS05 firmware compatibility findings

## Observed on the user's motor

The version-read response was `00 C4 56 00 05 00 03 07`. The manual identifies bytes 3–6 as the motor version: `00.05.00.03`, corresponding to 0.5.0.3. The final byte 07 is retained as raw data; no meaning is assumed here.

Motor ID 1 accepts type-17 reads for 0x7019, 0x701B, 0x701C, plus mode/current-limit readbacks during configuration. It rejects both watchdog-address read probes (0x7028 and 0x200C) with type-17 status 1. The preceding write of 0x7028 in the old jog test was queued, but the failed readback does not prove that the motor accepted or applied that write. No enable command was sent by the aborted jog test. Stop/reset feedback was received.

## Official release evidence

- [Firmware release V26.05.19](https://github.com/RobStride/Product_Information/releases/tag/V26.05.19)
- [Release's firmware update notes PDF](https://github.com/RobStride/Product_Information/releases/download/V26.05.19/default.pdf)
- RS05 section, version **0.5.0.5**, item 4: `类型17、18所有参数可读写` — all parameters readable/writable through types 17/18.
- This change postdates the observed 0.5.0.3 and explains limited parameter access on the user's motor. It is evidence for an interface/version mismatch, not proof that the old firmware lacks any internal timeout function.
- The inspected release publishes an RS05 **0.5.0.14** application image: [rs05_0.5.0.14.bin](https://github.com/RobStride/Product_Information/releases/download/V26.05.19/rs05_0.5.0.14.bin). The asset listing was verified. No firmware image was flashed or claimed tested in this cloud.
- The notes for 0.5.0.13 also mention a **CANopen** watchdog. That is a different protocol feature; it must not be presented as proof of private-protocol register 0x7028 behavior.
- [Current Python SDK parameter definitions](https://github.com/RobStride/Python_Sample/blob/cbf977e56c842d57a65f3f17c1b1ecaef002c424/robstride_dynamics/protocol.py) declare `CAN_TIMEOUT = (0x7028, uint32, canTimeout)`.
- The RS05 manual documents 20000 timeout counts per second; configuration and actual watchdog stop behavior still need on-device verification.

## Official update route, prepared but not executed

[MotorStudio releases](https://github.com/RobStride/MotorStudio/releases/tag/v1.0.3) provide [motor_toolV14L.zip](https://github.com/RobStride/MotorStudio/releases/download/v1.0.3/motor_toolV14L.zip). Inspection confirms a Windows `motor_tool.exe`. The official manual describes a compatible USB-to-CAN adapter, standby-mode parameter export, and selecting an application .bin under the device Upgrade control. The M5 Mini CAN unit alone is a physical CAN transceiver, not a USB-to-CAN adapter recognized by MotorStudio. Compatibility of any existing adapter must be checked; not every generic USB-CAN adapter speaks the serial protocol MotorStudio expects.

If the user chooses to upgrade using the supported route:

1. Keep one mechanically unloaded motor connected; use stable power and the correct adapter/driver documented by RobStride.
2. Connect in MotorStudio at the motor's existing ID/bitrate, leave it disabled, and read/export its current parameters before an update. Do not run calibration or reset-to-factory commands as an unrelated setup step.
3. Select the **RS05 CAN 2.0** application image, not RS05 CAN-FD, EL05, another model, or a factory HEX image. Confirm hardware/bootloader compatibility with the manufacturer if MotorStudio reports a mismatch; do not force it.
4. Follow the official Upgrade operation without interrupting power or communication. No remote flashing action has been authorized or executed by this research task.
5. Read version again; establish the post-update motor ID, bitrate, protocol, zero/reference and limits rather than assuming preservation.
6. Rerun the read-only watchdog diagnostic. Require successful read status before treating a value as real.
7. Configure and read back the timeout while disabled, then separately validate a communication-loss stop under controlled unloaded conditions. Register readback alone is not a validated watchdog.
8. Only resume the motion test when its prerequisites pass. Do not remove the watchdog gate just to make the prior jog run.

No physical test, firmware update, or calibration was performed by this cloud research. The user confirmed they have both a USB-to-CAN adapter and the Stick/Mini CAN setup. The next on-device step is the supported MotorStudio route using that adapter, subject to its documented driver/protocol compatibility. No untested custom flashing bridge has been substituted.

## MotorStudio screenshots and failed update handshake

The first update failed at 0% with `Failed to send bin file information!`. In that screenshot the device-selection dropdown appeared blank and the parameter table was empty, although motor feedback and adapter connectivity worked. The official OTA protocol requires device UID discovery, then an accepted OTA-start exchange, then an acknowledged file-size/packet-count exchange. App-mode feedback alone does not establish those OTA prerequisites.

Subsequent screenshots show a populated parameter table, app version 0.5.0.3, CAN_TIMEOUT at 0x200C displayed as 0, current limit 11 A, faultSta and warnSta 0, and boardTemp raw 333. Thus vendor-UI parameter access works using a path that the earlier type-17 probes did not. Firmware update is not yet proven necessary solely to configure a timeout. The first screenshot's missing apparent device selection is a plausible original handshake cause, not a confirmed diagnosis. After actual detection/selection and parameter loading, one new update attempt is a meaningful changed-state test; repeating unchanged failures is not useful.

If the same metadata error persists, capture the OTA start/info requests and replies rather than guessing about image or bootloader compatibility. The manufacturer's OTA document identifies message types 11 and 12 (decimal), with type 11 carrying MCU UID and type 12 carrying file byte/packet counts. Metadata failure may mean a negative reply or no expected reply; the GUI error alone cannot distinguish them. Do not substitute private-protocol parameter type 17/18 numbering for these OTA types.

`firmware/MotorStudioCapture/MotorStudioCapture.ino` (build target motorstudio-capture) is a strictly passive LISTEN_ONLY sniffer, with no TX queue and no transmit calls. It records raw CAN IDs, flags and payloads while the USB-CAN adapter and MotorStudio remain the active participants. It can observe READ actions without changing settings. Observing any subsequent update attempt must be deliberate; starting an update is not a read-only action merely because the sniffer is passive. The sniffer's compilation was verified; no physical capture has yet been obtained.

The user reports the update still failed after motor detection/parameter loading. Selection alone is not an adequate explanation. The next diagnostic is one deliberate update attempt with passive capture at **921600 serial baud** (CAN remains 1 Mbps), preserving raw request/response evidence. Add the M5 Mini CAN to the existing bus in parallel while all power is off; use CANH/CANL/common ground and preserve existing power wiring. Use separate USB COM ports for the M5 monitor and MotorStudio adapter. No erase, factory reset or calibration is part of this diagnostic. If update progress begins, let it finish without interrupting power. The sniffer itself sends no frames or acknowledgements.

## Captured legacy type-9 read interface

The user elected to keep firmware 0.5.0.3 and adapt the Stick code. MotorStudio's Refresh Para Table capture provides the first direct evidence of its successful configuration-read interface:

```
0900FD01  0000000000000000
090001FD  00000A00FFFFFFFF
090001FD  00000A01FFFFFFFF
090001FD  00000A02FFFFFFFF
090001FD  00000A03FFFFFFFF
0900FD01  0100000000000000
090001FD  01000A00FFFFFFFF
...
0900FD01  0010000000000000
```

This establishes type **9 decimal / 0x09** for this vendor-UI read path, extended CAN IDs using host 253 and motor 1, and a little-endian 16-bit requested index in payload bytes 0–1. Reply bytes 0–1 repeat the index; the returned name strings span multiple replies with changing header byte 3. Exact header byte 2 semantics, numeric encoding and error behavior are not yet established by these string reads. Do not claim that a raw zero payload alone is a successful watchdog read.

`firmware/LegacyParameterRead/LegacyParameterRead.ino`, build target `legacy-parameter-read`, sends the same type-9 read layout with host ID 126. It queries the screenshot-confirmed indices 0x200C (CAN_TIMEOUT), 0x2019 (current limit), 0x300C (voltage), 0x301F (board temperature), and 0x3022 (fault status). It logs all replies, checks the response type/address/index and records the two header bytes and raw 32-bit data without assuming status/type/scaling. It sends no write, stop, enable, erase or OTA frames.

For an on-device test, leave the motor disabled in MotorStudio, close MotorStudio and disconnect the USB-CAN adapter from the CAN bus before handing control back to the Stick. Retain the user's validated Stick/Mini CAN/motor wiring and power arrangement. Upload the separate sketch, open the Stick serial port at 115200 baud, power the motor and restart the Stick. Five read probes run once after three seconds. Capture output through DONE. Numeric results and watchdog access remain unverified until the returned frames are inspected.

## Scalar legacy replies verified on the user's motor

The user returned matching type-9 replies to host 126:

```
CAN_TIMEOUT 0x200C: 0C20040000000000
current_limit 0x2019: 1920060000003041
bus_voltage 0x300C: 0C3006000D2FC641
board_temperature 0x301F: 1F3003004D01FA00
fault_status 0x3022: 2230040000000000
```

These establish kind 4 = uint32 (timeout 0 and fault mask 0), kind 6 = float32 (current limit 11 A and voltage 24.772974 V), and kind 3 = signed int16 (333 tenths C = 33.3 C, per manual). Byte 3 is zero for these scalar replies. For kind 3 only bytes 4–5 are the value: trailing bytes 6–7 (`FA00` here) must not be interpreted as part of a 32-bit temperature.

The type-8 scalar write encoding was checked by static inspection of the official MotorStudio v1.0.3 `motor_tool.exe` (downloaded from its official release; not executed in the cloud). Its parameter writer builds payload index LE16, kind byte, fragment byte 0, and up to four value bytes; CAN type is 8, with host ID in bits 15–8. The type-9 read capture independently corroborates the header/value layout. This static evidence prepares the write test; it is not a physical successful-write claim.

`firmware/LegacySetup/LegacySetup.ino` (target `legacy-setup`) is a separately triggered configuration-only sketch. It sends only stop type 4, legacy read type 9, and scalar write type 8. On serial `c`, it requires reset/disabled feedback without current fault flags, verifies both existing register headers, writes only current limit 0x2019 kind 6 = 0.30 A and timeout 0x200C kind 4 = 4000 ticks (200 ms), and requires exact readback of both before success. It then confirms stop again. No enable, motion, erase, calibration, explicit flash-save or factory-reset command is included. Partial failure is reported; old settings are not silently restored or increased. Persistence across power cycling must not be assumed.

Use this on the already verified isolated Stick/Mini CAN/motor bench arrangement, with MotorStudio closed and its USB-CAN adapter disconnected from the bus. Upload with motor power off, open 115200 baud, power the motor and send `c`. Capture the entire output. Actual write acceptance and watchdog communication-loss stopping remain unverified until physical tests pass. Configuration readback alone does not establish stopping behavior.

Host verification:

```
g++ -std=c++17 -Itests/legacy-setup-host tests/legacy-setup-host/test.cpp -o /tmp/jumping-flash-legacy-setup-test
/tmp/jumping-flash-legacy-setup-test
```

Checks exercise the actual sketch's packet encoding, permitted register/type set, stop and header prerequisites, rejected writes, final readback and absence of enable/erase/save commands. Both these checks and the ESP32 build pass; no physical writes were performed by the cloud.

## On-device legacy configuration verified

The user ran LegacySetup and supplied a complete successful log. Stop feedback was reset/disabled without fault flags. Type-8 writes and subsequent type-9 reads verified 0x2019 kind 6 from `0x41300000` (11 A) to `0x3E99999A` (0.30 A), and 0x200C kind 4 from zero to `0x00000FA0` (4000 counts). Both values were reread successfully before a final confirmed stop. Thus read/write compatibility on firmware 0.5.0.3 is now physically verified; no firmware update is needed to access this watchdog setting. Physical timeout behavior is still unverified.

## Zero-speed physical watchdog test

`firmware/LegacyWatchdogTest/LegacyWatchdogTest.ino` (target legacy-watchdog-test) waits for serial `t`. It is a separate sketch using the user's existing lone-motor Stick/Mini CAN bus, with the other USB-CAN adapter disconnected and MotorStudio closed. Upload with motor power off, then power the mechanically unloaded, secured motor and use serial at 115200 baud. No enable is sent on boot. `x` or button A requests a stop. The test intentionally energizes the motor with a zero-speed command; small corrective motion or twitching is possible.

It confirms stopped mode, reapplies/readbacks 0.30 A and 4000 timeout counts via the now-verified legacy interface, configures velocity mode via the known standard interface, writes zero speed and verifies the actual speed command through legacy 0x3010, requires fault mask zero and a finite baseline position. Only then does it enable and require fresh RUN feedback. A control probe consisting of a zero-speed write must also report RUN: this checks that the probe itself does not stop the motor.

The host then forbids all CAN transmissions for 400 ms, while still servicing stop input and incoming frames. Unexpected traffic invalidates the test. After the gap it sends the same zero-speed probe without an enable and requires fresh reset/disabled feedback without fault flags. This observation occurs BEFORE its final explicit stop, so the final stop cannot be credited as watchdog action. It checks final position delta below 0.03 rad, confirms stop, and reports PASS only if every condition succeeded. Any missing/invalid feedback, observed fault, serial abort, unwanted traffic or failed prerequisite prevents a pass. It sends no nonzero speed target, save/erase/calibration command or OTA frame.

The test establishes a transition from verified RUN to verified disabled after a gap, not exact timeout latency, power-cut safety, or an emergency-stop circuit. Readback interprets 4000 counts as 200 ms according to the manual; the 400 ms test gap is not a measurement of the threshold itself. Actual motor behavior remains pending the user's physical log. If stopping is not confirmed, disconnect motor power.

Host checks:

```
g++ -std=c++17 -Itests/legacy-watchdog-host tests/legacy-watchdog-host/test.cpp -o /tmp/jumping-flash-legacy-watchdog-test
/tmp/jumping-flash-legacy-watchdog-test
```

The actual sketch is exercised with a simulator. Cases include successful watchdog timeout, a motor that ignores timeout (must fail), rejected configuration (no enable), serial abort, other CAN traffic, and a probe that itself stops the motor (must fail the control check). All transmitted speed targets are asserted zero; the quiet interval contains no transmissions. These checks and the ESP32 compile pass; they do not replace the on-device test.

### Passive watchdog test v2

The user physically ran v1: after its 400 ms silence and subsequent zero-speed probe, feedback was RUN; the explicit stop succeeded. Watchdog behavior is unverified. The post-gap probe could affect the observed state, so v2 removes it.

V2 configures a 1 A limit and 4000 timeout counts, verifies zero-speed velocity mode and no faults, and requires at least three automatic type-2 reports while disabled before enabling. The reporting switch is the official type-24 payload `01 02 03 04 05 06 01 08`; type-24 acknowledgements are not parsed as motor state. Reports to the configured master (possibly 253) are accepted. During a 1000 ms window the host sends no CAN frames. A pass requires observed RUN followed by disabled, reset observed within 350 ms of silence beginning, a disabled final state, no faults or foreign traffic, small measured position change, and final explicit stop confirmation. Unsupported reporting blocks enable. The reporting-off request is sent after the final stop; it is not verified.

Simulated checks cover automatic reports addressed to another master, ignored timeout, late reset, rejected configuration, unsupported reporting, serial cancellation, and foreign traffic. All speed targets are zero and TX during the quiet window is prohibited. ESP32 compilation passed. Actual behavior on 0.5.0.3 remains pending; no watchdog protection is claimed from simulation or parameter readback.

### v2 physical result and v3 timing correction

The user ran v2: current, timeout, and mode readbacks passed; automatic reports were not verified, so no enable was issued. Final stop succeeded. This is inconclusive about watchdog operation.

Research found a material documentation discrepancy: the official MotorStudio manual `使用说明书-Instructions/Instructions for using the Studio_241122.pdf`, section “CAN communication fault protection,” states 2000 counts = 1 second. The newer RS05 manual states 20000 counts = 1 second. The older manual's index layout differs from this RS05's observed table, so its timing is a hypothesis, not a proven firmware-specific specification. The first 4000-count / 400 ms physical test would be too short under the older scale (2 seconds). Prior 4000=200 ms statements must not be used as established behavior on 0.5.0.3.

V3 replaces the unsupported-reporting dependency with paired trials: timeout zero must retain RUN after 1500 ms silence; timeout 1000 must yield disabled after the same gap (1000 counts is nominally 500 or 50 ms under the two published scales). Same zero-speed probe is checked disabled and RUN before silence, so a probe that enables or stops the motor fails the controls. Current limit is 1 A and reapplied after enable; timeout readback after enable must remain configured. All speed writes are zero; no explicit stop is issued between the pre-gap RUN control and post-gap state probe. Each trial ends with explicit stop, and final cleanup attempts to restore/read back 1000 counts while stopped. Other host traffic invalidates the result.

This test observes state after a validated command, rather than passively timing a transition. Even a hardware pass would not measure timeout latency, prove loaded stopping performance, or guarantee behavior on every command. Follow with timing and actual communication-disconnection tests before relying on the safety function. Simulation covers both published scales, ignored timeout, rejected config, serial cancellation, foreign traffic, probe-induced enable/stop, and final cleanup. Physical v3 result pending.

### v3 physical result: timeout did not establish disable

The user's complete A/B log shows both trials configured and verified, disabled-probe and RUN-probe controls passed, and timeout survived enable. With timeout 0 the motor remained RUN after 1500 ms (control passed, delta 0.000297 rad). With timeout 1000 the post-gap probe also reported mode 2 / faults 0 (timeout trial failed). Explicit stops and final readback of timeout 1000 succeeded. No usable communication-loss disable has been established on 0.5.0.3 with this command path. Both published time scales predict expiry before 1500 ms; changing only those units no longer explains this result. The result does not identify a firmware bug versus undocumented conditions or prove physical torque behavior.

Recommended next route is resolve the official RS05 firmware-update handshake and retest, or obtain manufacturer confirmation of 0.5.0.3-specific requirements. Release notes confirm improved parameter access beginning 0.5.0.5 and later CANopen watchdog work; they do not explicitly promise a private-protocol timeout fix for this case. Do not claim the newer image fixes it until the same functional and physical disconnect tests pass. A controller-side timer cannot protect against controller failure or a broken CAN link; an independent power/enable cutoff is the fallback if the internal watchdog cannot be validated.

### Correction: the OTA-panel Erase button is part of update entry

After v1.0.2 failed with the same metadata error, the user found video descriptions directing Erase first. Static inspection of official v1.0.3 confirms the OTA-panel button at UI offset 0x4F0 is wired to `on_pushButtonInOtaMode_clicked` (0x4074F0), separate from factory reset. This handler has two states: initial click sets flag 0x5B08 and dispatches action 7 (0x423D70, type 14); subsequent invocation dispatches action 4 (0x424090, type 11 UID-start). Chinese UI changes the label to 启动升级; the inspected English branch retains Erase, so the transition can be confusing. Start Update dispatches action 5, type 12 metadata (0x423960). Thus advising the user to avoid the OTA Erase control categorically omitted a relevant prerequisite. Do not claim a single click's exact physical flash effect or calibration preservation; allow the vendor GUI's prompts/state changes to guide the sequence. No button was clicked and no hardware update executed from the cloud.

### Firmware update succeeded; first post-update v3 attempt aborted before enable

The user reports OTA Erase followed by update succeeded first try and the new version looks correct. The subsequent v3 log confirms initial STOP and legacy 2019/200C readbacks, then `READ FAILED type=17 index=7005`, followed by unconfirmed STOPs. No enable was sent in this attempt: execution failed before that point. This is not a completed watchdog test. The old completion text “Motor should be disabled” was misleading and has been replaced with explicit final confirmation/failure wording.

Inspection found the old pump unconditionally aborts on type-21 fault/warning frames, even if their masks are both zero. It also assumes a particular reply-address layout. Neither is yet established as the cause of this physical log. PostUpdateDiagnostic logs all raw replies and decoded type-21 masks without suppressing reads. It accepts only stop, identify, type-9 read and type-17 read requests; no transmit on boot and no enable, write, clear-fault or calibration path. Trigger r at 115200 after upload with motor power removed, then repower for reads. The user has been instructed to disconnect motor power because the last stop was unconfirmed. Host simulation verifies the transmit whitelist and raw retention, including alternate-address frames and zero-mask type-21 reports. ESP32 build passes. Await raw device output before selecting a protocol fix or another enable test.

### Raw post-update capture and RS05-specific diagnostic v2

The user's raw capture confirms STOP mode0/flags0, normal type-17 response IDs 0x1100017E, run_mode0, limit_cur1.0 A, canTimeout1000, finite position, and legacy timeout read1000. No type-21 frame appears. Responses disappear starting with legacy fault-status read; final STOP has no reply. Therefore the unconditional type-21 abort is a real code issue but is NOT established as the cause of this run. Lost CAN acknowledgements, firmware behavior, timeout expiry, and the legacy query path remain hypotheses. TX ESP_OK means queued, not proof of an acknowledged motor command.

The user correctly cautioned against using RS01-specific sources. Robstride01.cpp had been consulted for shared framing/report-enable sample code, not RS05 electrical limits, but that sample does not establish RS05 behavior. The new configuration is grounded in the RS05 CAN2.0 manual `RS05User Manual260713.pdf`, standard table 0x7028 uint32 / 20000=1s, and standard telemetry indices 0x7005/0x7018/0x7019/0x701C, plus the RS05 upper-table 0x3022/3023 definitions. Configured 1000 counts is nominally 50 ms under that manual; the prior raw capture deliberately left 350 ms gaps, which is a possible confound rather than a proven cause.

PostUpdateDiagnostic v2 sends no enable, target, current-limit write, save, fault clear or calibration. After fresh fault-free disabled feedback it writes ONLY standard timeout 0x7028=20000, verifies readback and uses standard reads with 80 ms windows and TWAI error counters. It leaves the timeout at20000, with no action claim until separately tested. Initial stop failure blocks the setting change; final stop failure explicitly requests power disconnect. Host checks and ESP32 compilation pass. Actual user run pending.

### Standard diagnostic v2 physically passed; next standard-only watchdog A/B v4

The user supplied complete v2 output: type-18 timeout20000 write acknowledged in disabled mode, type-17 readback 0x4E20 successful, mode0/current1 A/position0/voltage24.597528 V read successfully, and final disabled feedback mode0 flags0 confirmed. TWAI state1 throughout; tx_error_counter declined7 to0, rx_error_counter0, tx_failed_count stayed1, missed0. Thus no further failed transmissions were logged during this run; the initial failure's cause remains unidentified. Upper-table type-17 indices3022 and3023 explicitly return id_status1: their zero payloads are not valid fault/warning readings. Do not continue trying to read these via type17 despite the broad release-note wording.

LegacyWatchdogTest now emits STANDARD WATCHDOG A/B v4 and uses only standard requests: current7018, timeout7028, mode7005, speed700A, position7019. No legacy or upper-table fault reads. Paired zero-speed trials test timeout0 stays RUN, then timeout20000 becomes disabled after1500ms command silence. Masks in received type21 frames are checked rather than aborting on zero masks. Prerequisite state flags must be clear. Current1 A is verified and reapplied after enable; timeout retention is verified after enable. Probe controls, silence, position-delta check, explicit stops and final nonzero configuration remain. Exact timing and actual cable-disconnect behavior still require separate verification. Host simulation (including zero-mask fault frames) and ESP32 build pass. Physical v4 result pending.
