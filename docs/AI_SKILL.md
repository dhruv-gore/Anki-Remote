## Skill: Maintain Flipper GUI + BLE HID Apps Safely

### Scope
This skill captures the key implementation rules and pitfalls discovered while
building the Anki Remote app with a vertical Keynote-style controller UI and
BLE HID behavior.

### Core Rules
- **View draw callbacks use the View model, not the View context.**
  - Always allocate a model for each View and store the app pointer there.
  - Guard draw callbacks against null models.
- **ViewOrientationVerticalFlip changes both drawing and input mapping.**
  - Input is remapped by the ViewPort; labels should invert that mapping so the
    UI reflects how the device is physically held.
- **Match stock UI assets for pixel-perfect layout.**
  - Use the exact firmware assets (e.g., `Space_60x18.png`) rather than cropped
    variants to avoid clipped borders and off-by-one rendering.
- **BLE HID teardown order matters.**
  - Stop advertising, disconnect, then restore the default profile.
  - Avoid calling `furi_check` on teardown paths if the profile may not be active.
- **Long Back behavior should always exit.**
  - Do not map long Back to HID; suppress the release event after exiting.

### Reference Implementation Checklist
- View models:
  - `view_allocate_model(..., ViewModelTypeLockFree, sizeof(AnkiRemoteViewModel))`
  - `((AnkiRemoteViewModel*)view_get_model(view))->app = app;`
  - `view_commit_model(view, false);`
- Controller labels:
  - Invert vertical flip mapping for displayed button names.
- BLE HID lifecycle:
  - Start: `bt_profile_start(...)` → `furi_hal_bt_start_advertising()`
  - Stop: `furi_hal_bt_stop_advertising()` → `bt_disconnect()` → `bt_profile_restore_default()`
- Exit behavior:
  - Menu Back should stop the view dispatcher.
  - Controller long Back exits and ignores release.

### Common Failure Modes
- **Crash on first render:** draw callback uses context instead of model.
- **Crash on exit:** BLE profile teardown called while inactive or after exit.
- **UI clipping:** non-stock assets or incorrect canvas assumptions.

### When to Apply
Use this skill whenever:
- Adding new View-based UI screens.
- Changing screen orientation.
- Modifying BLE HID start/stop behavior.
