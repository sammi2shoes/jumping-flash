# AX12 cold-start discovery probe — APK v0.6.2

[Download APK](../releases/JumpingFlash-AX12-v0.6.2.apk). Install over the existing app; no Stick reflash. Motor control profiles and failsafes are unchanged. Cold-start input recovery is now user-confirmed on the AX12; broader reliability validation remains pending.

## Evidence and change

The supplied v0.6.1 cold-boot log verified raw921600/8N1, but received **zero bytes**: no accepted, rejected or CRC frames. Local baud configuration succeeded; there is no evidence yet that a discovery handshake alone solves the missing stream.

Static analysis of the supplied RadioMasterAX native ARM64 library found a fixed periodic link-discovery packet: `A6 07 2B 04 FF 01 F4`. AppComHub::onTimeOut (0x19cc4a8) sends group0x2b, source4, destination0xff and payload01 through sendPack2 / QCommUsart::writePack. CRC8/MAXIM over bytes1–5 is F4. Library SHA256: `75ef3bcc49736e0b8b1fd1045c784adeca6b42e353585931a4480dc00141cafd`. This establishes stock use of this packet, not that it starts control reporting. Unknown radio/model/RF configuration traffic is not replayed.

Unlike v0.6.1, the live UART opens read/write and exposes one native transmit function with this fixed seven-byte packet only. With no accepted control report, it probes after250ms, then at intervals of at least2seconds, at most three attempts per reader session. An accepted control report permanently ends probing for that session, even if reports later become stale. A transmit failure closes the reader. Passive capture remains read-only and preserves terminal settings.

A discovery response never creates live controls. Existing CRC/layout/value validation,120ms freshness limit, disarm behavior and deliberate rearming remain required. Exports now include session duration, probe count/transmitted bytes, first256 received bytes and the last CRC-valid frame.

## Cold-boot test

1. Keep both motor supplies OFF. Force stop the stock RadioMasterAX app.
2. Install this APK, cold boot the AX12, and launch only Jumping Flash.
3. Open Tools → Live input monitor. Wait about5seconds, then move gimbals, dials and switches.
4. If inputs remain NOT LIVE, export the session log. Do not launch the stock app before exporting; that would change the startup state being investigated.

## Reported hardware result

The user reported live inputs immediately on the first v0.6.2 cold boot, then passed all three requested repeat cold-boot checks without opening RadioMaster. These four reported starts confirm the startup behavior on this AX12. They do not independently isolate which initialization operation caused recovery, measure startup latency, or establish interruption recovery and competition reliability. No motor watchdog or disconnect result is inferred from these input-only checks.

## Software verification

Android build, lint and67 unit tests pass. Tests cover bounded probes, stopping after a valid report, cancellation, transmit failure and discovery frames being excluded from controls. Linux pseudo-TTY tests verify raw configuration, exact fixed transmitted bytes, exclusive access and the read-only passive path. Firmware/host checks pass. These checks do not emulate the AX12 radio MCU.
