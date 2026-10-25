# AX12 v0.3.0 passive internal-input prototype

[APK download](https://github.com/sammi2shoes/jumping-flash/raw/refs/heads/main/releases/JumpingFlash-AX12-v0.3.0.apk). Install over the existing app; named mappings/calibration are retained. Requires Android9/API28+ and ARM64 (the user's AX12). Matching BLE robot firmware remains unimplemented. This is a diagnostic capture, not a cable-free motion-control release.

## One transmitter test

1. Keep robot/model outputs disarmed. Let the stock RadioMaster app start normally once so it can perform its usual initialization. Remove the external DSC-to-USB cable.
2. Open Jumping Flash → **Native inputs → Enter probe mode**. Our BLE link closes and robot commands are blocked. This block survives activity/app restarts until **Exit probe mode** is selected; it does not control the AX12's independent ELRS transmitter.
3. Select **Stock app settings**, tap Android's **Force stop** for RadioMasterAX, then return to Jumping Flash. Do not uninstall the stock app. Close any other serial readers. Confirm the ownership checkbox. It resets on focus/background changes.
4. Keep **/dev/ttyS0** for the first test. Tap **Start45s passive capture**. Sweep S2 first; then other controls one at a time. Candidate raw values, counters and source age appear on this page. The alternate ttyS4 is listed by the inspected stock app, but should only be tested if needed after reviewing the first result.
5. Tap **Stop capture** or let the45second/128KiB limit stop it. Open **Diagnostics → Export diagnostic log** and send the file, even if the result is permission denied, busy, silence or unrecognized frames. The export includes access metadata, raw RX bytes, current baud/termios metadata (queried after opening) and timestamps.
6. Return to Native inputs and tap **Restore stock RadioMaster app**. It waits for our reader to close before launching the stock app. This restores normal startup ownership and clears the Android force-stopped state. Later, **Exit probe mode** returns our app to its previous input path, disarmed, requiring fresh inputs and deliberate rearming.

The probe itself never force-stops another app, changes permissions, invokes root, writes UART bytes, changes baud/termios, calibrates the transmitter, or sends configuration/RF commands. Restoring the stock app invokes its ordinary behavior. Force stopping the stock app may affect native RC operation; this is an explicitly stopped-model bench test.

## Ownership and lifecycle

JNI opens only ttyS0/ttyS4 with O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC, verifies a character device, then requests TIOCEXCL before reading. This blocks subsequent ordinary opens; it does **not** evict a reader that was already open or prove the stock app stopped. The operator confirmation is therefore required. If the driver's exclusive request fails, the fd closes without reading. No competing-reader fallback or automatic privilege change exists.

One worker owns the fd. It polls in100ms windows, uses512byte reads, and closes/releases the fd in finally. STOP, focus loss, backgrounding and destruction request stop. Size/time limits also stop capture (time limit has poll/loop granularity). Restoring the stock app waits for closure. BLE connection selection, all arm/jump entry points and every command send are blocked in probe mode. Decoded probe values never populate the gamepad/named-control state.

## Framing and interpretation

The supplied APK decoder recognizes A3/A6/A7 envelopes. This prototype handles one-byte lengths for A3/A6 and two-byte little-endian lengths for A7, bounded to1024bytes and a2048byte scan buffer. It checks CRC8/MAXIM over bytes1..penultimate with initial0; XOR-only matches are counted separately. Unrecognized CRC variants, corrupt or unsupported frames remain in the raw capture for analysis, not silently accepted as live controls.

Only CRC-valid A6/A7 records matching RC group0x10/source2/destination4, subtype1 and a payload at least93bytes receive a **Sept-layout candidate** display. That display reads42 signed little-endian16bit sources from the raw block seen in the APK. Known candidate indices are RUD0, ELE1, THR2, AIL3, S1=12, S2=13, SB21, SC22, SD23. 0x8000 displays unavailable. These labels/layouts still require live hardware confirmation. XOR-only, wrong-route and older87byte records never refresh the candidate source age. A known published legacy87byte capture verifies CRC/framing without claiming current-layout compatibility.

This implementation sends no poll/keepalive. The MCU may require an initialized app session, particular serial settings or polling to stream at a useful rate. Silence alone does not prove the port is wrong. An open-port metadata query records current baud and termios flags without changing them. No writes will be added based on guessing; review the capture first. Future motion integration must verify source freshness, values, rates and recovery independently of BLE heartbeats.

## Validation / rebuilding

- APK assemble/lint and44 JVM/Robolectric tests pass, including arbitrary fragments, resynchronization, bad CRC/lengths/routes, XOR separation, source age, port-denied/busy/unavailable paths, time/size bounds, cleanup, output blocking, backgrounding, STOP and activity recreation.
- A Linux pseudo-TTY test uses the actual C I/O core to verify O_RDONLY, failed writes, subsequent-open exclusion, unchanged termios, exact bytes, timeout, ownership release and hangup handling. This is not an AX12 UART/kernel test.
- Native ARM64 code builds with Android NDK r27c27.2.12479018/API28. ELF imports contain open/read/poll/ioctl/close and metadata helpers, with no write, tcsetattr or system call. APK signature/checksum are published. Actual AX12 permissions, stream format and exclusivity remain unverified.

Normal Gradle builds use the committed tiny JNI library and check its source/binary SHA256 stamp. To rebuild changed C source on Linux:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r27c
scripts/build_ax12_uart_probe.sh
cd android/ax12
./gradlew assembleDebug lintDebug testDebugUnitTest
```

Host C test: `cc -Wall -Wextra -Werror tests/native-uart/port_test.c -o /tmp/port-test` then `/tmp/port-test`. No vendor library or code is shipped in this APK.
