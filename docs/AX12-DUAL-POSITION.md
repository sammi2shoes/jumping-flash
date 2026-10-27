# Two-leg fixed position bench — v0.1 / APK v0.7.0

**Historical v0.1 procedure.** [v0.2 / APK v0.7.1](AX12-CAN-RECOVERY.md) replaces the manual watchdog gate with automatic setup and bounded CAN recovery.

This replaces the unloaded direction test with **BOTH-motor position control**. Right RS05 ID1 extends toward decreasing encoder angle; left RS05 ID2 extends toward increasing angle. Both use CAN2.0 firmware0.5.0.14. No gait, jump, steering mix or ESC output is implemented. Begin with the currently bare outputs; attached mechanisms/springs are not accepted by this release.

## Downloads and flashing

- [APK v0.7.0](../releases/JumpingFlash-AX12-v0.7.0.apk): install over v0.6.3. Retains cold-start discovery, input monitor and temporary-overlay BLE retention.
- [Stick ZIP with source, binary and instructions](../releases/AX12DualPosition-v0.1.zip)
- [App-only binary](../releases/AX12DualPosition-v0.1-app.bin)

Both motor batteries OFF; Stick connected by USB. Extract the ZIP, open PowerShell in that folder, close Serial Monitor, and substitute the Stick port for COM3:

```powershell
py -3.13 -m esptool --chip esp32 --port COM3 --baud 460800 write_flash 0x10000 AX12DualPosition-v0.1-app.bin
```

**Offset0x10000, not0x0.** This is an app-only update for the existing AX12SingleLeg/DirectionTest partition layout. Packaging verifies partition compatibility. Do not erase flash/NVS; existing BLE bonds can remain. Other partition layouts require separate setup. Each Stick reboot discards references and watchdog qualification. The Stick's battery can keep it alive after USB is unplugged, so USB removal alone is not a reset.

## First bare-output test

1. SB disarmed; CH1/CH2 centered. Power both motors, connect BLE, and run **Tools → Check BOTH motor watchdogs**. Each motor is briefly enabled in velocity mode at ZERO commanded speed/1A, with timeout0 and20000 comparison; the test restores position mode afterward. Keep outputs clear.
2. With both motors disabled, choose and mark a mock retracted pose on each bare output. Later the real linkage must be placed at its known fully retracted pose without driving it into a physical stop. This is a human declaration of pose, not automatic homing.
3. Release Stick B, then hold it for2seconds. Both positions must stay stationary (within0.002rad over the final100ms). The Stick records two independent encoder offsets. No encoder-zero, calibration, persistent-save or erase command is sent. Confirm **POSITION CONTROL READY** and Stick REF VALID.
4. Center controls; cycle SB off, then drive-armed. Both motors enable at their current bounded positions. A small elevator input extends/retracts both with mirrored signs. SC is reserved; changing it still disarms. CH1/CH3/S2/SD do not command actuators.
5. Neutral keeps each current **target** and actively holds position. Check small travel, stop at midstroke, rearm, then test the fixed0–80degree range. SB off/Stick A/serial x disables BOTH. Loss/staleness also disables, so outputs can move freely under external force when disarmed. This is not passive spring retention.

Limits:1A each,1rad/s target rate and motor position-speed limit, no launch profile. Targets clamp independently to each fixed0–80degree interval. A0.15rad following-error guard stops both; feedback outside the interval by more than0.02rad or an encoder jump over0.3rad between reads invalidates trust. Arming requires measured positions within0.002rad of the allowed interval. These software checks do not establish actual loaded stopping distance or mechanical limit protection.

## Reference and reconnect policy

Arm/rearm never recaptures zero. Ordinary SB stops and confirmed Bluetooth/input-loss stops preserve the independent offsets. A new arm preloads both measured bounded positions before enabling either; it does not return to an old target. Both must remain in range. Holding B again cannot overwrite this boot's captured offsets.

A motor restart/identification broadcast after capture, CAN/read/settings uncertainty, unexpected mode/fault, or out-of-range/jumping feedback invalidates reference trust and blocks position arming. Restart the Stick and manually set both known retracted poses again. Origins are intentionally not persisted across motor/Stick resets. No software-only method guarantees absolute pose across arbitrary encoder reset or mechanical slip; competition requires a physical reference strategy.

After actual BLE loss, reconnect manually, wait for fresh inputs/telemetry, cycle SB off and then arm deliberately. Neither reconnect nor input recovery automatically moves the motors. Temporary Android interruptions retain BLE but disarm; the v0.6.3 behavior remains.

## Verification and logs

Host simulation exercises independent fixed origins, mirrored targets, endpoint clamps/hold, midstroke rearm, BLE loss, failed motor/watchdog, and uncertainty/no-recapture. Android checks cover capability128, required-reference arming and UI. Firmware is compiled for the Plus2 with the pinned official ESP32 core. Hardware acceptance of this new position build is pending.

Telemetry capability128 means dual-position bench, not gait capability. Flag bit1 means BOTH references valid; ready bit0 means configured, qualified and fresh feedback; bit5 means watchdog checks pending and bit6 test failed. The app requires ready AND referenced before arm, shows separate left/right angles, and preserves the latest commissioning flag summary in exports even after the rolling input log advances. That summary is historical evidence with a timestamp, not proof of current freshness or per-trial raw traces. Voltage is the lower of the two packs. Protocol still has20bytes/version2; maintenance requests remain explicit disarmed actions.
