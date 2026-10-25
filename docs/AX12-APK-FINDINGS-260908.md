# Findings from RadioMasterAX.260908.apk

This report analyzes the user's actual supplied build, not just the independent research. Two upload parts were concatenated into the analysis ZIP; all15 selected file hashes matched the preparation index. The helper reports original APK SHA256 `11ff890cef1febe720248d45858a6d670a90ccc6312ee1d71764ba3795af2a87`; the complete original APK was not transferred, so that whole-file digest and its v2/v3 signature have not been independently verified. Selected native-library SHA256 is `75ef3bcc49736e0b8b1fd1045c784adeca6b42e353585931a4480dc00141cafd`, ELF build ID `cc9b3f8744bbb0245748dffabb4bc50a5aec1167`.

Static inspection only: decoded binary manifest with Android aapt, decompiled DEX with JADX1.5.3, inspected AArch64 ELF symbols/disassembly using pyelftools/capstone, and recovered853 Qt resource entries including QML, help and the bundled Lua archive. No vendor code was installed, run, patched or sent to the transmitter. Vendor binaries/resources remain outside the repository; this report contains findings and small analytical excerpts only. Machine-readable addresses/hashes: [260908-summary.json](ax12-analysis/260908-summary.json).

## The stock app has a cable-free native source path

The Android activity is mostly a Qt launcher/permissions shell. Core code is in libRadioMasterAX_arm64-v8a.so. The native UART receive path uses QCommUsart and AppComHub; AppRadioControl::umbusPackReceived updates the RcSysSrcRaw source cache and signals sourceUpdated. The receiver copies an84byte raw-source block on its live-input branch, then calls RcSrcUpdate and updates the Lua stick adapter. Do not assume the old research's frame layout applies to every message in this newer build; wire fields still need device capture validation.

The stock calibration page responds to sourceUpdated and calls AppRadioControl.getSrcCfgList(). The native method obtains values via RcSrcGetRaw(int). RcSrcCalibrate.qml uses source-list index12 for S1 and13 for S2. RcHome1.qml uses21/22/23 for SB/SC/SD. These indices are zero-based internal source indices, not Android axis IDs, RC output channel numbers or Lua IDs.

| Control | Native/QML source index | Lua alias/ID |
| --- | --- | --- |
| Rudder / unused fourth gimbal axis | 0 | rud /87 |
| Elevator | 1 | ele /88 |
| Throttle | 2 | thr /89 |
| Aileron | 3 | ail /90 |
| S1 | 12 | s1 /91 |
| S2 | 13 | s2 /93 |
| SB | 21 | SB /127 |
| SC | 22 | SC /128 |
| SD | 23 | SD /129 |

This explains the user's observation: the native source display and the DSC-to-USB gamepad route are different interfaces. The gamepad was supplied by the physical cable setup; it must not be described as an automatically available internal Android input device. Native source getters already cover the controls we need, but they execute in the stock app's process.

## Ordinary cross-app API: not found in this inspected build

Manifest package com.Flyshark.RadioMasterAX, versionName1.0/versionCode1, minSdk28 and targetSdk34. Exported AndroidStart is the launcher. A boot receiver is exported for boot broadcasts. BootStartService exists, is not exported, returns null from onBind, launches the main activity and stops itself. The FileProvider is not exported and serves sharing, not live channel state. No declared bound channel-data service or provider was found. This corrects the older research's statement that there were no services at all.

The Java application classes do not supply a channel subscription interface. Native QCommTcp::openConnect is an outbound TCP connection, not evidence of a channel server we can subscribe to. The native symbols and Qt singletons are not Binder/IPC APIs: loading the vendor library inside our APK would create separate process state, not attach to the stock app's live source cache.

## Lua is not a ready-made S2 bridge in this build

The app supports third-party Lua tools under Internal Storage/AX12LUA/SCRIPTS/TOOLS, and includes getValue/getSourceValue/getFieldInfo registrations. However, names existing is not enough:

- luaFindFieldByName recognizes s2 and resolves it to source ID93, also recognizing SB/SC/SD.
- The registered getValue handler at0x1b8e9d4 resolves strings and calls getValue(int,bool*).
- That backend at0x1b2a97c has nonzero data cases for IDs88/89/90 (ele/thr/ail) and returns zero for other IDs. Thus the inspected implementation returns zero for s2 and the switch IDs regardless of their real native source values.
- The registered getSourceValue handler at0x1b8eb34 calls the same limited backend. It is not an alternate route around that limitation.
- getFieldInfo can identify a source without proving its value getter works. A zero from a recognized dial is therefore not evidence the dial is centered or unavailable in hardware.
- The bundled simulator uses only ail/ele/thr, consistent with those implemented cases.

A basic Lua getValue('s2')→file bridge would be misleading on this binary. We have not run it on the device and do not claim a working Lua bridge. Its file API is also a custom OpenTX-style io.read(file,n)/io.seek(file,offset) wrapper rather than normal Lua file methods; the seek wrapper narrows offsets to32bits, so a generic Linux/Lua memory-reading example would not be a suitable shortcut either.

## Another misleading symbol: rcJoystickMap

Disassembly of rcJoystickMap(int*,int*,int) at0x19f1410 shows a four-element permutation selected by a0..3 mode value. This is gimbal Mode1/2/3/4 arrangement, not demonstrated arbitrary routing of S2 into a USB HID slot. We found no verified user-facing knob-to-Android-axis configuration in the inspected QML. Our previous proposed remapping route remains unproven, not an available feature to instruct the user to select.

## Recommended implementation direction

The viable data source is the internal radio-channel path, rather than more Android gamepad detection. Before coding an active native input driver, establish how our APK can legitimately access it and own/cooperate on the connection:

1. A vendor-provided/cooperative channel API would preserve stock-app ownership and is preferred if available; none was found in this static inspection.
2. A dedicated internal-UART input service is technically plausible, but it must be the sole reader while active. Port permissions, process ownership, initialization requirements, live packet format/rate and recovery must be verified on the AX12. Do not open a competing reader alongside the stock app; it can consume the other's bytes. The uploaded APK alone cannot establish runtime file permissions or concurrent-read safety.
3. Patching/instrumenting the stock app is a separate option with update/signing/maintenance costs; it has not been implemented or installed. No root or transmitter firmware changes have been made.

The first hardware prototype should display and log native inputs with all robot outputs blocked, prove S2/SB/SC/SD states with the external cable removed, and verify input-source freshness. Only then connect it to the existing named profile and BLE commands. A fresh BLE heartbeat alone does not prove the upstream input feed is alive. No new motor/ESC firmware or competition performance claim follows from this static analysis.

## Reproduce the native inspection

```sh
python -m pip install -r scripts/ax12-analysis-requirements.txt
python scripts/inspect_ax12_native.py path/to/libRadioMasterAX_arm64-v8a.so --output native-inspection
```

The tool reads ELF bytes without loading the library, records relevant native function bodies with call-target labels, and recovers text/script resources from this build's AArch64 Qt version3 resource wrappers. It is not a general decompiler or automatic semantic proof. Validated against the supplied library:853 indexed resource entries, zero resource errors, and the recovered S2 calibration/source page cross-checked against the native function inspection. Requirements are pinned to the versions used. Vendor code/binaries are not committed to the repository.
