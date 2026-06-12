# Changelog

## [4.1.1-beta] — Portable Companion App

### Added
- Single-file build for the companion web app using `vite-plugin-singlefile`.
  - The entire app is now bundled into a single `companion-app.html` file.
  - Supports direct execution from the filesystem (`file://` protocol).
  - No local server required for basic usage (Web Serial still requires a compatible browser).

### Changed
- Updated Vite configuration to use relative base paths for better portability.
- Refreshed `release/companion-app.html` with the latest portable build.

## [4.1.0-beta] — Companion App Beta

### Added
- New companion web app (React + TypeScript + Vite) for configuration without reflashing.
  - Real-time gamepad visualization.
  - Axis/button mapping editor.
  - Calibration for trigger axes (min/max capture).
  - Configurable telemetry subscription (`app sub telemetry <ms>`).
- JSON command protocol between companion app and firmware (`app get/set/ping/sub/unsub`).
- EEPROM-backed configuration persistence with magic number + versioning.
- New modular firmware architecture:
  - PIO-based CRSF transport.
  - Dedicated CRSF frame parser.
  - RC processing pipeline with smoothing and deadband.
  - Enhanced dual-core usage (Core 1 = CRSF RX/parser, Core 0 = USB HID + CLI).
- Serial passthrough mode for ELRS/Betaflight configurator workflows.
- Expanded telemetry reporting: RSSI, LQ, SNR, uptime, heap, loop Hz, processing latency.
- Auto-recovery watchdog for passthrough mode.

### Changed
- Monitor baud behavior aligned to companion-web (`115200`).
- CLI restructured around typed JSON responses consumed by the companion app.
- Documentation translated to English and extended for companion-app usage.

### Fixed
- Improved telemetry stability and reduced noisy updates.
- More reliable RC path handling with per-axis smoothing and deadband.
- Restored calibration and telemetry optimization behavior.
