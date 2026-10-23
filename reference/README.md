# Earlier PC controller reference

`rs05_control_gui.py` is the unmodified Python GUI supplied by the user during this session. The user reported successful motor movement with it. It is retained as a protocol/motion-sequence reference, not the current robot firmware.

Known limitations found during review: type-17 parameter reads do not check the reply error status, and the watchdog is written without a functional verification. Successful GUI motion does not establish watchdog protection. Do not use this historical controller as evidence of the final failsafe behavior. Its built-in self-tests use simulated hardware.
