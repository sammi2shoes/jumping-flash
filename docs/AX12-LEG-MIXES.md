# Single-stick leg mixes — APK v0.7.3 / Stick v0.4

[APK v0.7.3](../releases/JumpingFlash-AX12-v0.7.3.apk) · [Stick v0.4 ZIP](../releases/AX12DualPosition-v0.4.zip) · [app-only binary](../releases/AX12DualPosition-v0.4-app.bin)

Both motor outputs remain supervised, enabled together and individually limited. **CH1 aileron / CH2 elevator on one transmitter stick** provide the leg inputs. **SC** selects the mix. SB remains arm/disarm. No on-screen profile selection, gait, hop or spinner output is added. Install BOTH matching builds; older dual-position firmware reserved/ignored SC and did not implement these mixes.

| SC | Mix | CH1 / CH2 behavior |
| --- | --- | --- |
| Low | Side assist | Elevator extends/retracts both. Aileron left adds extension to LEFT only; right adds extension to RIGHT only. Aileron alone extends its selected side. |
| Middle | Differential | Elevator extends/retracts both. Left aileron adds left extension and right retraction; right aileron reverses that. |
| High | Select a leg | Aileron left selects LEFT; right selects RIGHT; centered aileron selects BOTH. Elevator controls extension/retraction of the selected leg(s). Aileron alone holds both. |

Positive normalized elevator means extension, preserving the existing calibration/direction. Select-a-leg uses aileron less than−25% for LEFT, greater than+25% for RIGHT, and the inclusive middle range for BOTH. Elevator and aileron each retain an8% firmware input deadband after the app's existing normalization. Combined rates clamp separately to±1rad/s; a diagonal input cannot exceed the existing speed limit. Each target independently clamps to its own fixed0–80° interval, so one leg reaching a limit does not prevent the other from moving.

Neutral means active target hold, not automatic return to equal leg angles. When switching sides, a leg may finish following its last bounded target before settling. Rates are commands, not guaranteed measured velocities.

## Formulas

With normalized deadbanded elevator E and aileron A (left negative, right positive), outputs L/R are independently clipped to[−1,+1]:

- Side assist: L=E+max(−A,0); R=E+max(A,0).
- Differential: L=E−A; R=E+A.
- Select a leg: L=E unless A>0.25, otherwise0; R=E unless A<−0.25, otherwise0.

For example, full retract plus full right aileron in side-assist cancels the RIGHT retract command while LEFT retracts. In differential mode, one side may cancel or saturate on diagonal input. No vector normalization slows the opposite leg just because one side saturates.

## Switching and startup

Select SC with **SB disarmed and the stick centered**, then arm deliberately. A SC change while armed disarms BOTH and requires cycling SB off/on. It never recaptures or overwrites reference origins. The existing200ms arm stability window, immediate disarm, background watchdog configuration, bounded CAN recovery and hard-fault checks remain.

Flight Deck shows SC low/middle/high and mix name, measured leg travel, and a signed left/right **input preview**. The preview describes the current stick/mix even while disarmed; it is not an assertion that actuators are enabled, and is hidden when input reports are stale. Controls explains the three modes. Existing radio-profile role metadata may retain the future hop/gyro/ambulate names; on this matched bench build SC selects only the mixes above.

App-based reference capture remains **Flight Deck → Set both retracted zeros** with SB off, centered controls and both outputs physically at their known retracted poses. No Stick button is needed. [Reference and rapid-SB details](AX12-SB-RECOVERY.md).

## Flash / first check

Install the APK over the current app. Both motor supplies OFF while flashing. Extract the ZIP and run from its folder:

```powershell
py -3.13 -m esptool --chip esp32 --port COM20 --baud 460800 write_flash 0x10000 AX12DualPosition-v0.4-app.bin
```

App-only0x10000 update for the established partition layout; no NVS erase. On first acceptance keep outputs bare, connect with SB disarmed/centered, set both references in the app, and begin with small movements. Check LOW left/right aileron individually, MID opposite motion from mid-travel, and HIGH selection followed by elevator in both directions. Return SB off before each SC selection. Limits remain1A per motor,1rad/s and fixed0–80°. No loaded testing, current increase or launch profile is part of this update.

## Validation

Firmware compilation, full host suites, Android build/lint and78 Android tests pass. Motor simulations cover left/right-only side assist, independent neutral hold and endpoint clamp, differential opposite travel from midstroke, high-mode selected-side retraction/extension and aileron-only hold, selector boundary values, SC-change disarm with origins preserved, plus previous three-minute dropped-reply/recovery tests. Java preview tests cover each formula and clipping; Activity tests verify all three Flight Deck mix names. These are software tests; physical v0.4/v0.7.3 acceptance is pending.

No BLE frame format change: command style byte7/telemetry echo still carries SC index0–2. The dual-position firmware now interprets that index as the leg mix; capability128 still identifies the bench controller. Other firmware builds retain their own style semantics.
