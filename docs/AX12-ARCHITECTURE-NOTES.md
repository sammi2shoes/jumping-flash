# AX12 Android/radio boundary and missing dials

## Sources and strength of evidence

- User-provided transcript of Mads Tech teardown/deep dive: https://youtu.be/mAG7dg8O1Hw. Transcript reviewed; the video could not be retrieved directly in this environment.
- User's AX12 Android9 v0.2.1 input probe: stock radio monitor responds to S1/S2, but the all-source generic-motion/key trace records no dial events, only ACTION_CANCEL. The FLYSHARK Composite VCP and Joystick exposes eight Android axes; no separate rotary device is listed.
- Independent reverse-engineering project, revision b938319b6dcba5e3c694ef7aeede00a1ca40ffd2: https://github.com/rmeadomavic/ax12-research/tree/b938319b6dcba5e3c694ef7aeede00a1ca40ffd2. Documents UMBUS/internal channels and native symbols. These details are not all demonstrated by the Mads transcript and are not a public SDK guarantee.

## What Mads demonstrated/reported

| Video time | Finding | Scope |
| --- | --- | --- |
| 19:22–19:33;22:13–22:38 | Identifies an AT32 controller and attributes RC processing to it, separate from the Android processor | Attribution expressed as his hardware/test-based inference |
| 24:01–24:42 | Closing the stock RadioMaster app did not interrupt the tested ELRS link | Does not establish that another Android app keeps receiving joystick data |
| 24:49–25:49 | ADB Android stop/start did not interrupt the tested ELRS link | Not a power cycle; not Bluetooth operation |
| 26:18–27:05 | Android reboot interrupted ELRS | Shared power/reset behavior is his proposed explanation |
| 27:12–31:15 | After reboot, ELRS resumed around stock-app startup; with the app uninstalled it did not resume in his test | Suggests startup configuration/enable dependency; exact enable protocol is not supplied |

The transcript supplies no Android S2 axis assignment, vendor API, joystick-mapping menu procedure, or UMBUS framing. It does not establish unchanged behavior across all transmitter firmware versions. It is not evidence that an Android Bluetooth control application can continue operating when Android stops.

## Consequences for this project

Keep the stock app installed and its normal startup behavior intact. Do not replace it or take over its serial port based only on these observations. Our existing Android gamepad input route demonstrably works for the mapped gimbals/switches; the missing dial route remains unresolved.

First investigate whether the stock app can route S2 to an unused Android joystick axis. The reverse-engineering project documents a rcJoystickMap symbol, but a symbol is not proof of an available user-facing setting or callable external API. If remapping is unavailable, identify a supported vendor input/channel interface or a cooperative bridge before implementing native dial access. Competing reads from the stock app's serial port are not an acceptable default implementation. Its simulator bridge is a separate root-based implementation and must not be confused with the user's working FLYSHARK joystick device.

For robot control, a live BLE connection/heartbeat proves the app/link is active, not that the upstream input source is producing fresh values. Android joystick change events alone do not reliably distinguish stationary sticks from a frozen input feed. Any native channel integration should expose input-source freshness as well as values, including dial data. Robot-side command timeout, all-actuator stopping and the RS05 CAN watchdog remain necessary regardless of the native ELRS link's independence. These notes do not claim that the matching BLE motor/ESC firmware has already been implemented.
