# Changelog

## [Unreleased]

### Fixed

- `HeadTracking.log` is rotated to `HeadTracking.prev.log` on launch, so a
  crash-then-relaunch no longer destroys the session worth reading
- Capped the camera hook's "waiting for player camera instance" line at 6
  reports; it repeated every 5 seconds forever (about 43 KB per hour) in
  exactly the situation a user would be logging
- `uninstall.cmd` now removes `HeadTracking.log` and `HeadTracking.prev.log`

### Changed

- Replace the single `[Sensitivity] Smoothing` key with `LocalSmoothing` (default 0.0) and `RemoteSmoothing` (default 0.15), selected per connection from the packet source address
- Remove the `[Position] Smoothing` key: position now uses the same connection-selected value as rotation
- Remove the hidden 0.15 baseline smoothing floor, so local trackers get zero-latency tracking by default
- The tracker owns the centre. The recenter hotkeys (`Home` / `Ctrl+Shift+T`),
  their `RecenterKey` / `ChordRecenterKey` INI entries and the mod-side centre
  capture are gone; the tracker pose is applied as absolute. Centre the view in
  your tracker app instead.
