# Preparing the vendor APK for offline inspection

The user supplied RadioMasterAX.260908.apk. The attachment-to-workspace tool rejected it because its transfer limit is32MiB. The actual APK has **not** been downloaded or inspected; existing architecture/native-symbol findings remain third-party research until verified against the supplied build.

Use the standard-library Python helper scripts/prepare_ax12_apk.py on the computer containing the APK:

```powershell
py prepare_ax12_apk.py "RadioMasterAX.260908.apk"
```

It reads the ZIP container, preserving the original APK. It packages the manifest, resources, DEX, application-specific native libraries from the preferred ABI, scripts/config resources and available v1 signer-certificate entries. Qt framework/plugin binaries and unrelated assets are omitted. An index lists every APK member and hashes selected files plus the source APK. No APK code is installed, executed or patched.

The output is AX12-analysis/AX12-analysis.zip. If it exceeds28MiB the helper also writes numbered .part files of at most28MiB; upload all those parts instead of the oversized ZIP. They must be concatenated in numeric order before opening the ZIP. These bundles are analysis inputs, not installable APKs. APK v2/v3 signature verification cannot be performed on the repacked bundle; a source SHA256 identifies the original bytes but does not establish signer authenticity.

Once received, inspect Android entry points/exported components and permissions; DEX/JNI/native loading; UART/channel dispatch and source getters; Lua APIs and script lifetime/background behavior; and any usable cooperative data-export interface. Distinguish read paths from writes that modify radio model, calibration, RF or power. Native symbols are not an external IPC API and separate processes do not share a native library's in-memory channel state. Do not install a patched stock app or take over its serial port as part of static analysis.

Preparation helper tests cover ABI/file selection, unsafe-entry exclusion, source preservation, exact member bytes and bounded part splitting/reassembly. No binary-analysis conclusion is implied by those tests.
