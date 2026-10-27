# CAD-based input mapping — requested future interface

The user wants a mapping page built around the robot's CAD, with the mechanism moving as controls are exercised. This is a future UI feature, not implemented in APK v0.6.1. The immediate foundation is verified live inputs and measured motor telemetry.

The intended flow is to select a leg/joint/mechanism on the model, assign a channel/switch/action, and see its requested motion. A separate overlay should show the motion inferred from fresh measured feedback, making tracking error visible. Stale, missing or unreferenced feedback must not be drawn as confirmed physical pose. Current direction-test angles are relative to the start of a test run, not calibrated leg coordinates.

For the biped model, ID1 is right and ID2 is left, with mirrored linkage. Model data will need the physical motor axes, link lengths/pivots, mounting transforms, direction signs, reference conventions and limits. A render mesh (for example GLB) is useful for Android display; CAD assembly/joint information or separate kinematic metadata is needed to animate the four-bar mechanism correctly. An ankle spring's deflection/foot contact cannot be measured from motor angle alone; a visual spring/foot model must distinguish assumed geometry from measured state.

Mapping/learning should be a disarmed preview workflow with no actuator/jump requests. Runtime pages should clearly distinguish requested targets, measured motor state and simulated geometry. CAD geometry does not replace motor limits, reference checks, link-loss handling or physical acceptance tests. No CAD export/upload is required to finish the present input-startup fix and monitor.
