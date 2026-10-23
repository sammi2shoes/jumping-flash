# Single-leg prototype: fixed reference and held enable

Current MotorRecovery v0.15 targets StickC Plus2, MiniCAN GPIO32TX/GPIO33RX, RS05 CAN2.0 firmware0.5.0.14, motor1/host126,1Mbps. Build environment motor-recovery. It supersedes the per-arm relative reference in v0.9 and earlier. The current prototype has no locking mechanism. One leg is attached; the spring can extend the mechanism when torque is disabled. No coordinated two-leg jump, spinner output, second leg or automatic homing is implemented.

## Startup and controls

1. Boot with motor disabled, connect the controller and wait for SET ZERO: B 2s. No reference is captured automatically.
2. With the leg mechanically supported against its ankle spring, manually position it at the known fully retracted pose. Center the controller stick and release R1. Release the Stick's side buttonB, then hold it for2seconds. Two encoder reads100ms apart must be stationary within0.002rad. This checks stillness, not whether the operator selected the correct pose. No driving into hard stops occurs.
3. Wait for ZERO CAPTURED and READY. Center the stick, then press/hold R1 to enable. Left-stick X right extends toward80deg; left retracts toward0deg. Extension decreases encoder position.
4. Keep R1 held: neutral ramps target velocity to zero and actively holds the resulting position. Release R1, press front buttonA, send serialx, or lose/stale Bluetooth input to request torque disable. R1 remains a held enable; the proposed toggle was cancelled. Disable can release stored spring energy; active hold is not a mechanical brake.

Optional sequences: after normal zero/arming, keep R1 held and left stick centered. A fresh **controller Y** press runs the original slow cycle; a fresh **controller X** press runs slow retract / faster extension. These are controller face buttons, not Stick side buttonB used for zero. See v0.15 below.

Manual/slow-cycle current remains1A, speed1rad/s and host target-rate acceleration5rad/s^2. Only the X-cycle extension/endpoint settling uses3A,5rad/s and40rad/s^2. Travel0..80deg and motor watchdog20000counts (nominal1s) remain. Holding torque at1A has not been validated against this spring. The fixed0/80deg limits apply to every arm within the referenced session. After disarm the target is preloaded from the new measured position, not replayed, while the original zero remains unchanged. Center/release then fresh R1 press is still required after a stop/reconnection. There is no output for the spinner ESC.

## Reference trust and recovery

The manual reference is assigned once per Stick startup. Buttons cannot overwrite it after capture. Normal confirmed stops and Bluetooth disconnect/reconnect preserve the zero and continue position/settings monitoring while disabled. Before arming, position must remain in range and control mode, current limit, speed limit and watchdog must read back correctly. These settings are also checked periodically during active/disabled monitoring.

CAN read/target failures, an unconfirmed stop, faults, unexpected disabled feedback while active, changed/unreadable settings or a motor identification broadcast invalidate the reference. The numeric zero is retained for diagnostics, but motion is blocked. Reference cannot be recaptured within that session: restart, restore the known retracted pose and repeat the manual procedure. After uncertain stopping,1250ms without control transmissions allows the motor watchdog to expire before recovery. A confirmed ordinary stop instead continues disabled monitoring, since polling cannot re-enable an already confirmed disabled motor.

This is not a physical home sensor or a reset-proof reference. Zero is in RAM, not persisted through Stick power loss. A motor reset that preserves settings and returns a plausible in-range encoder value could escape these software checks. Any known motor power cycle/reset requires restarting and manually referencing, regardless of the display. An independent home/reference sensor and reset detection are needed to guarantee physical reference integrity across all resets. Calibration, motor-zero setting, fault clearing, parameter saves and automatic enable are never issued.

## Validation

v0.15 host simulation covers required manual reference, short/unsafe button holds rejected, held R1 and neutral position hold, both80deg endpoints, unchanged zero through mid-stroke disarm/rearm and Bluetooth reconnection, recapture blocked, CAN/settings uncertainty blocking enable without changing zero, readback tolerances and mailbox handling. Hardware validation of this version is pending. Prior v0.6 logs establish observed motion/R1-release disable; the user reported later v0.9 motion, controller-loss stop/reconnect and correct direction with the leg attached. Those reports do not validate this new fixed-reference implementation or holding torque under spring load. Historical version notes below describe earlier behavior and are not current operating instructions.

## v0.2: Bluetooth connection classification and diagnostics

The first hardware attempt reported paired Bluetooth but PAD LOST/STALE and no movement. That is correctly gated movement, not a reason to relax input freshness or increase current. The connection callback filtered isGamepad before retaining the handle. Bluepad32's official Controller example retains the handle in onConnected and checks its class when processing reports; class data may not be available until input arrives. v0.2 retains the connected handle, then still requires real gamepad reports and the existing200ms freshness limit before accepting controls. This fixes a plausible dropped-handle path; the actual user's cause remains pending updated device output.

A once-per-second PAD diagnostic prints physical link, gamepad class, freshness, report count/age, leftX, RB_R1 and phase. Display distinguishes NO LINK / WAIT TYPE / NO REPORTS / FRESH / STALE. The motor remains gated by the same freshness, neutral-input and release/press rules. Regression simulation covers late classification and connected-without-reports refusing to arm, plus prior motion/recovery/failsafe cases. ESP32+Bluepad32 build passes. Download releases/MotorRecovery-v0.2.zip; the unversioned MotorRecovery.zip is refreshed to match. Hardware verification is pending.

## v0.3: CAN transmit mailbox pacing

The v0.2 hardware log showed repeated configuration failures with confirmed STOP replies, plus PAD link=0 and no reports. With the software TX queue disabled, consecutive sends could encounter an occupied hardware mailbox. v0.3 waits up to20ms for the preceding transmission before sending another frame, services input/stop handling during that wait, and keeps single-shot transmission and the queue disabled. A blocked mailbox, driver error, failed TX, or mismatching parameter readback now prints a specific diagnostic. This addresses a plausible transport failure; on-device confirmation is pending.

Host simulation now models a busy mailbox between frames and verifies bounded failure when it stays busy. The ESP32 build passes. Bluetooth discovery is explicitly enabled; pairing keys are preserved. The current controller is an 8BitDo Ultimate2, whose exact variant and mode still need identification; no compatibility or pairing fix is claimed. Bluetooth freshness and motor arming gates are unchanged. Download releases/MotorRecovery-v0.3.zip.

## v0.4: Bluetooth first

Startup attempts STOP once, then displays CONNECT PAD and waits for fresh Bluetooth gamepad reports before any configuration writes or normal motor polling. Pair/reconnect the controller as usual; stored pairing keys remain intact. After input arrives, the disabled motor configuration is verified, then the existing centered-stick release/press RB/R1 gate applies. Reconnection after a loss also waits for fresh input. This does not force a controller into pairing mode or assert Ultimate2 compatibility. Simulation verifies no configuration traffic while waiting, followed by normal deliberate arming. Hardware confirmation remains pending.

## v0.5: bounded position-target readback tolerance

Hardware v0.4 reports confirmed fresh Ultimate2 gamepad input, ACTIVE state with joystick changes, shaft movement, and disabled confirmation on R1 release. One recovery failed exact position-target verification: wanted3D25CB84 (0.040477291rad), returned3D26334A (0.040576257rad), a difference0.000098966rad. The source of the difference is not established. Only index0x7016 now accepts finite readback within0.0002rad (0.0115deg); other settings retain exact comparisons. Simulation covers this measured-size discrepancy across startup/active operation, larger mismatch rejection, nonfinite rejection and existing failsafe paths. Speed remains0.10rad/s, current1A, travel+/-5deg; this is not continuous-rotation firmware. Hardware verification of v0.5 is pending.

## v0.6: smoother unloaded bench motion, +/-20 degrees

At the user's request, relative travel is now +/-20deg around each deliberate arming position (40deg total range). Current remains1A and maximum target rate0.10rad/s (5.73deg/s). A host-side rate ramp limits changes to0.50rad/s^2, including starts, neutral transitions and reversals. At a travel boundary the target is clamped and its rate reset. Release R1, stale input, faults and explicit stops bypass the ramp and request disable immediately. Arming resets the ramp; no prior rate is restored. Serial MOTION lines show measured/target displacement and target rate in degrees.

Full-stick travel from center to20deg takes about3.6seconds with the ramp; this is not continuous rotation or the calibrated0–80deg leg mode. Keep the mechanism disconnected. The current position mode (CSP,5) and watchdog setting are retained. Simulations exercise both20deg boundaries, gradual starts/reversals, existing stops/recovery and readback handling; hardware validation remains pending.

Reference reviewed: [JumpRopeStick](https://github.com/amcchord/JumpRopeStick/tree/2e916574c885e0a7f2b75004d01ce229965df853), especially main/sketch.cpp, controller_manager.cpp, motor_manager.cpp, web_server.cpp and robstride_protocol.h. Useful ideas include motor-role mapping, mode state machines, telemetry/dashboard separation and PP positioning. Its automatic fault clearing, mechanical zero setting and re-enabling are not adopted. No source was copied. RS05-specific [manual260713](https://github.com/RobStride/Product_Information/blob/384cc561e1ff079c22f68b19908526094430febd/Product%20Literature/CAN2.0/RS05/RS05User%20Manual260713.pdf), sections3.4.4 and4.1.14, confirms PP mode1 and speed0x7024/acceleration0x7025 (marked write-only). Switching to PP remains a separate validation task, including watchdog behavior in that mode.

## v0.6 hardware motion result

User-supplied [session log](bench-logs/motor-recovery-v0.6-motion.txt) starts at the v0.6 banner; earlier v0.4 idle lines from the same pasted upload are omitted. Startup STOP confirmed, Bluetooth connected and delivered fresh reports, settings became ready, and a deliberate R1 press entered ACTIVE. Motion reached-20.02deg for a-20deg target and+20.00deg for a+20deg target. Moving samples show tracking lag up to about1.2deg at the selected low speed; these sparse samples do not measure peak error. R1 release was followed by disabled confirmation, configuration recovery and READY with R1 released. No TX/readback failure was logged during this run. Android-profile fallback did not prevent fresh gamepad input. This establishes unloaded motion and the observed release/disable sequence, not combined controller/CAN-loss timing, loaded behavior or physical torque-removal timing.

## v0.7: faster unloaded bench test

User confirmed the motor shaft is still unloaded. Maximum target rate and configured motor speed limit increase to1.0rad/s (57.3deg/s), with host target-rate acceleration5.0rad/s^2. Travel stays+/-20deg relative to each deliberate arm; current stays1A and timeout20000counts. All freshness, release/press arming, readback verification, stop and recovery gates remain. From center, the target reaches either boundary in approximately0.45seconds at full stick, including the ramp; this does not predict actual motor arrival or stopping time. Neutral ramps target rate to zero, which can add travel; releasing R1 requests disable immediately. These relative bench limits must not be used with the linkage attached.

Version0.6 movement and R1-release disable are hardware evidenced above. Version0.7 speed, tracking and combined link-loss behavior remain hardware validation tasks. Host checks cover faster ramp/both limits and existing failures; ESP32 compilation passed. No full-power, continuous-rotation, jump or linkage control is introduced.

## v0.8: 80-degree unloaded sweep

After the user reported v0.7 seemed good and requested the full80deg, the bench interval becomes0..+80deg relative to each arming position. Right stick-X direction (positive axis) increases the target; left returns toward0. Negative targets are clamped at0. Encoder-positive direction is not yet verified as physical CCW from the linkage's chosen viewing side. This is an80deg total range, not+/-80deg. The initial arming position is the lower endpoint; releasing/rearming captures a new relative origin. No permanent zero or locked-start calibration is set.

Speed remains1rad/s, current1A, host acceleration5rad/s^2, watchdog20000counts, freshness200ms and existing deliberate arming/stops. Full-stick target sweep takes about1.5seconds from rest; actual arrival depends on tracking. Measured travel monitoring now applies separately to both endpoints with the existing0.035rad guard margin. Simulation covers attempted negative travel at startup, both80deg endpoints, rate ramps and prior recovery/failure paths. v0.8 hardware validation is pending. Use only the disconnected/unloaded shaft; this is not the installed leg controller.

## v0.9: reverse physical extension direction

At the user's direction, extension now decreases the encoder position (EXTEND_SIGN=-1). Controls and displayed logical travel remain right=extend toward80deg, left=retract toward0deg. Motor targets occupy origin-80deg through origin, and measured endpoint monitoring uses the same direction mapping. Target rate is expressed in logical extension coordinates; the displayed positive travel does not imply increasing encoder position or independently establish physical CCW. Speed/current/watchdog and arming behavior are unchanged. The reference still resets on every arm; this remains unloaded bench firmware. Regression simulation explicitly checks negative encoder travel to the80deg endpoint, return to origin, lower-bound rejection and previous faults/recovery. Hardware direction confirmation is pending.

## v0.10: fixed manual reference; held R1 retained

The user cancelled the proposed R1 toggle. This release retains held enable and adds the once-per-startup manual reference, fixed limits, settings checks and reference-invalid lockout described above. Per-arm zero capture has been removed.

## v0.11: controller cancellation must not masquerade as CAN failure

The user reported REF INVALID after switching off the controller. A reproducible v0.10 race explains one possible cause: Bluetooth loss inside an outstanding position read or target readback cancels that operation; callers then treated the false return as transport failure and invalidated zero even when STOP was confirmed. No complete hardware log of the failure was supplied, so this is a matched/reproduced software defect, not proof of the only hardware cause.

Operations now distinguish input cancellation from actual transport/readback/state failure. Position reads, target verification, settings checks and arming carry that reason to the disarm handler before STOP transactions overwrite it. Only input cancellation with no fault/reset evidence and a confirmed STOP preserves reference trust. Timeouts, rejected reads, mismatching/nonfinite values, TX errors and unconfirmed STOP remain invalidating. Interrupted CAN operations print Input cancelled read or input cancelled transaction instead of claiming feedback loss. Reference capture, fixed limits and held R1 behavior are unchanged.

Regression simulation reproduces controller power-off during position0x7019, target0x7016 and current-limit0x7018 reads, plus enable-state confirmation. Each retains the identical origin, stops, refuses automatic re-arm, and permits a fresh release/press after return. A cancelled operation followed by unconfirmed STOP still invalidates. Full host checks and ESP32 compilation pass; hardware confirmation is pending.

## v0.12: one-motor sequence rehearsal

The user confirmed v0.11 fixed the reported controller-off reference problem and that1A holds the current lighter-spring test rig. No new raw logs or heavier-spring validation were supplied. The R1 toggle proposal remains cancelled.

This version adds one deliberately triggered retract/pause/extend cycle at unchanged1A,1rad/s and5rad/s^2 target-rate acceleration. While ACTIVE, hold R1, center the left stick and freshly press controllerY. If Y was held through startup, arming or reconnection, it must be released and pressed again. Off-center requests are discarded rather than queued; presses while a cycle is running do not queue another cycle.

Stages: retract toward fixed0deg with a braking rate profile; require commanded endpoint and measured arrival within0.5deg; hold within that measured band continuously for300ms; extend toward fixed80deg with braking rate profile; require endpoint and measured arrival before COMPLETE. Completed cycles actively hold80deg while R1 remains held. Holding Y never repeats. A fresh release/press starts another cycle. The first test may be started partway extended to observe retraction.

Joystick deflection cancels the cycle and resumes manual control from measured position. R1 release, StickA/serialx, controller loss/staleness and faults use existing stop paths and discard every cycle stage/request; re-arming never resumes the old cycle. A stage that cannot reach/hold its endpoint within6seconds requests disable; it does not proceed to extension on elapsed time alone. Fixed reference and all existing transport/readback checks remain in force. The display/serial log identify MANUAL, RETRACT, PAUSE, EXTEND and DONE/HOLD.

This is a single-motor sequence rehearsal, not a full-power jump or coordinated two-leg launch. Spinner output remains absent. Host simulations cover one-shot/held-button behavior, both endpoints, manual override, off-center trigger rejection, R1/controller cancellation without resume and a stalled-motion timeout. Existing fixed-zero, link-loss cancellation and CAN-failure tests pass; ESP32 compilation passes. Hardware verification of v0.12 is pending.

## v0.12 hardware cycle result

The user's pasted log shows two completed retract/pause/extend/hold cycles, joystick override into manual control, then a third retraction interrupted by R1 release followed by Disabled confirmed and READY. [Selected exact log lines](bench-logs/motor-recovery-v0.12-cycle-excerpt.txt) preserve the evidence. An initial arm reports76.95deg rather than rebasing the current pose to0deg. Retraction pause reports0.09deg; completed extension samples report79.95deg and80.00deg. No CAN/readback/reference errors appear in the supplied log. Bluetooth remains fresh throughout this test; this run is not a controller-loss test.

Moving samples show commanded versus measured differences up to4.89deg (31.89 versus27.00 on retraction), and4.86deg (73.48 versus78.34 on extension). Sparse untimestamped samples do not establish peak tracking error, actual acceleration, physical settling duration or launch energy. Endpoints converge, but transient tracking needs measurement before higher-speed launch tuning. Current remains1A for the lighter-spring test rig; no new motor settings or code changes were made from this result.

## v0.13: timestamped current/velocity/tracking trace

Motion settings, zero capture and controls are unchanged from v0.12. While ACTIVE, TRACE CSV records are emitted nominally10Hz. Filtered Iq0x701A and encoder velocity0x701B are read for each trace; bus voltage0x701C is refreshed nominally2Hz. These registers are documented read-only float32 values in the RS05 manual section4.1.14; velocity/voltage previously worked on hardware, while this new filtered-Iq read awaits confirmation. Reads are sequential, not simultaneous. Additional CAN/serial work may affect actual loop intervals; no hard real-time sampling claim is made.

Header:

```text
TRACE_HEADER,t_ms,pos_ms,cycle_at_pos,pos_deg,target_at_pos_deg,error_deg,command_deg,command_rate_deg_s,vel_ms,vel_deg_s,iq_ms,iq_A,vbus_ms,vbus_V
```

`t_ms` is row output time. Individual `*_ms` timestamps indicate when responses were processed, not hardware-latched simultaneous sample times. `pos_deg` and `vel_deg_s` are in the logical extension-positive coordinate. `target_at_pos_deg` is the target that was already active during the position read; `error_deg` is that target minus measured position. `command_deg`/`command_rate_deg_s` are the subsequently updated command. `cycle_at_pos` identifies the stage at the position read, which can precede the later stage-transition message. `iq_A` retains the motor's reported signed filtered Iq; it is not battery current or a measured peak-current waveform. `vbus_V` is motor-reported bus voltage.

Earlier MOTION rows compared an earlier position read with a newly advanced target. At1rad/s, one50ms command increment alone is about2.86deg, so the v0.12 observed4.9deg difference cannot all be assigned to physical tracking lag. This version compares the prior target to position and logs the new command separately. The once-per-second MOTION summary is renamed target_at_pos_deg accordingly.

Unsupported optional telemetry (read status1) is disabled for that startup and reported as nan with timestamp0, never decoded as a valid zero. Nonfinite optional values are similarly unavailable. Transport failures/timeouts, other read errors and input cancellation retain existing stop/reference classification; motor settings/position/target checks are not relaxed. Sample caches clear on disarm. A missing optional field cannot prevent an otherwise verified control link from being stopped or held.

Procedure: upload, manually reference with StickB as usual, arm holding R1, keep the joystick centered and press controllerY for one cycle. Copy the serial output, including TRACE lines, through cycle completion and R1 release. One cycle is enough for the first check. No new software or library installation is required. Serial remains115200baud. This trace is intended for the current slow rig test, not resolving millisecond launch peaks.

Regression simulation verifies trace output, register values/timestamps, alignment to the already-active target, unsupported-current data staying nan without repeated reads, and controller loss during all three telemetry reads retaining reference only after a confirmed stop. Existing cycle/zero/failsafe checks and ESP32 compilation pass. Hardware validation of v0.13 is pending.

## v0.14: bounded target-readback tolerance adjustment

A [user log excerpt](bench-logs/motor-recovery-v0.13-target-mismatch-excerpt.txt) shows arming verification stopped on index0x7016: wanted0x40B122C3=5.535493374rad, returned0x40B12532=5.535790443rad. The difference0.000297070rad (0.01702deg) exceeded the former0.0002rad threshold. Disabled feedback was confirmed and reference trust was conservatively invalidated; that flag did not mean software reassigned zero. The precise source of the readback discrepancy has not been established.

Only finite position-target readback now permits an absolute difference up to0.0005rad (0.02865deg). For scale, the RS05 manual's14-bit absolute encoder gives2pi/16384=0.0003835rad per full-turn count; this comparison does not prove the command register quantization or a measured geared-output resolution. The tolerance is a small bounded allowance informed by the observed discrepancy. Current, speed, watchdog and byte-mode setting verification remain exact. Larger target discrepancies, nonfinite values and actual transport/reset failures still stop/invalidate as before. Fixed-zero capture, travel endpoints and current/speed limits are unchanged.

Tests include the exact hardware hex pair, near-threshold acceptance, a0.0006rad discrepancy rejected, NaN/infinity rejected, and the full startup/motion/cycle/recovery suite with the observed-size offset. ESP32 compilation and host checks pass. Mismatch diagnostics also print delta/tolerance in radians. The recovery message now explicitly says restart when the reference is invalid, rather than suggesting the B button can overwrite an existing session zero. Upload/restart and manually reference again before the next cycle trace. Hardware confirmation of v0.14 and filtered-Iq telemetry remains pending.

## v0.14 hardware trace result

The [captured session](bench-logs/motor-recovery-v0.14-trace.txt) begins at the v0.14 banner; prior-session idle lines were excluded. It includes manual zero capture at6.933346rad, a later arm at approximately79deg without rebasing zero, one completed retract/pause/extend/hold cycle and R1-release disable/READY. No target mismatch, reference invalidation, read error or unsupported telemetry message appears. Filtered-Iq, velocity and voltage reads returned finite values.

[CSV](bench-logs/motor-recovery-v0.14-trace.csv), [computed summary](bench-logs/motor-recovery-v0.14-summary.json), and [plot](bench-plots/motor-recovery-v0.14.png) preserve51 samples with median100ms position intervals. Maximum sampled absolute tracking error is2.551deg (extension), versus2.214deg during retraction. Sampled filtered-Iq magnitude reaches0.5745A in retraction and0.2783A in extension. Extended-hold mean sampled magnitude is approximately0.200A. Recorded voltage is24.141–24.176V. Median position-difference speed during the selected full-rate moving samples is-56.7deg/s retract and+55.57deg/s extend, close to the57.30deg/s target rate. Motor velocity readback varies substantially, including small values at stationary endpoints; position differences provide an additional motion check.

The final extended samples reach80.000deg; the largest sampled positive excursion is80.014deg. Sampled Iq does not show sustained saturation at the1A setting, but its filtering and10Hz sample rate do not rule out brief current limiting/peaks. The trace does not measure jump energy, heavier-spring load, physical torque-removal time or high-speed dynamics. There is no evidence here requiring a higher current limit. A subsequent incremental speed trial could retain1A and fixed endpoints while increasing speed; no firmware/motor-setting change was made from this analysis.

## v0.15: Y slow baseline; X asymmetric launch-profile trial

The user requested a visibly faster extension to build confidence, then specified the existing Y cycle should be considered slow. Y therefore retains1A/1rad/s retraction and extension. ControllerX selects a separate asymmetric cycle: retract at1A/1rad/s, hold the retracted pose within0.5deg for300ms, verify the3A/5rad/s motor profile, then extend toward the same fixed80deg endpoint with40rad/s^2 target acceleration/braking. All cycles require centered stick, held R1 and a fresh face-button edge. Simultaneous Y/X selects slow; held buttons never auto-trigger on arm or repeat.

For an ideal trapezoidal target profile,80deg at5rad/s with40rad/s^2 acceleration/deceleration takes about0.404seconds. This is planned target timing, not a measured physical stroke or a jump-height prediction. Retraction is deliberately still slow. The profile is not the motor's maximum speed/current. Keep the spinner off and clear of the rig/linkage; the single motor and spring may kick or hop the test platform. A side-on high-frame-rate video plus TRACE output is useful for the first single X-cycle trial.

The extension and high-profile endpoint hold request control updates every10ms (versus50ms otherwise); target integration dt is capped at20ms during those stages. TRACE requests rise nominally to20Hz there, with per-field timestamps still required. Actual loop times can exceed the request because reads, display and serial work are sequential. TRACE appends profile_A,profile_rad_s,control_ms,cycle_mode; the profile fields are the latest verified limits, control_ms is the uncapped observed control interval and cycle_mode is SLOW/FAST.

Once the target and measured position reach80deg within0.5deg, the fast cycle enters BRAKE/HOLD and holds that endpoint on the burst profile until it remains within the band for150ms. It then verifies restoration of1A/1rad/s before declaring COMPLETE. This preserves braking/holding authority until observed settling, rather than lowering current merely when the target reaches80deg.

A one-second software burst deadline starts when the high profile is verified and includes extension and BRAKE/HOLD. Expiry requests STOP; it is not a certified hard real-time torque-removal guarantee. The independently tested motor CAN watchdog remains configured at20000counts. Joystick override verifies low limits before manual motion. R1 release, controller loss or stop cancels all cycle state; after confirmed ordinary STOP, pending/high profile limits are restored while disabled. If profile changes fail or STOP cannot be confirmed, reference trust remains blocked and the existing quiet/watchdog recovery applies. Every arm verifies base limits before enable and reapplies them afterward. Mode/current/speed/watchdog monitoring follows the expected verified profile instead of mistaking intentional profile changes for reset evidence. Zero never changes during these operations.

Host simulation covers Y remaining slow, X reaching the faster target rate, braking/settling before current restoration, no repeat/auto-launch, simultaneous-button behavior, joystick/R1 interruption, cancellation during an unverified high-current write, rejected high-profile writes blocking extension, and a stalled fast cycle stopping on its deadline. Full regression checks and ESP32 compilation pass. No v0.15 hardware run or physical launch performance is claimed. Fixed-zero repeatability, stronger-spring loads, thermal margin and loaded stopping remain separate physical validation tasks.
