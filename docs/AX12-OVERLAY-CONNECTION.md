# Temporary Android interruptions — APK v0.6.3

[Download](../releases/JumpingFlash-AX12-v0.6.3.apk). Install over the current app; no Stick update.

The user reported BLE disconnects when pulling down system UI or plugging in USB. Inspection found that every Activity onPause explicitly closed GATT, including temporary overlays. v0.6.3 moves that close to onStop, when the activity actually becomes invisible. Focus loss and pause still disarm and clear pending motion; pause still stops the native input reader, which resumes automatically on foreground return. This does not enable background robot operation or automatic rearming. Temporary pauses preserve the existing BLE session; actual link failure still requires manual reconnect. Fully switching away, screen-off or destroying the activity still closes BLE. An overlay that Android treats as a full stop can therefore still disconnect.

## Hardware check

Both motor outputs are currently bare. First test with SB disarmed: connect the Stick, pull down/dismiss notifications, and plug/unplug AX12 USB. For a temporary pause/focus change the link should remain connected. Inputs must become live again on return; an interrupted arm must require SB off before rearming. Then check an actual app switch still disarms/disconnects. Do not interpret preserved BLE as continued permission to move. USB power or real radio faults are not ruled out by the lifecycle change.

The existing firmware blocks movement until both per-boot motor watchdog checks qualify. The user confirmed DIRECTION TEST READY and correct two-motor motion; this confirms qualification through the firmware gate, not separate raw trial traces.

Android lifecycle tests verify pause retains the modeled BLE-ready session while clearing arm, resume does not rearm, and full stop closes the session. Physical overlay/USB behavior remains to be checked.
