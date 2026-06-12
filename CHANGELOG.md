# Changelog

## [4.1.3-beta] — Mapping Fixes & Processing Unity

### Fixed
- **Axis Mapping Mismatch:** Fixed data structure mismatch between firmware (array of numbers) and Web Companion (array of objects), preventing correct mapping application.
- **Unified Processing:** Refactored `RC_Processor` to use a single `processAxis` method for all 6 axes (replacing the redundant `processThrottle`), ensuring consistent smoothing, deadband, and scaling.
- **HID Axis Order:** Standardized HID report axis order (X, Y, Z, RX, RY, RZ) for better compatibility with simulators and games.
- **EEPROM Config Persistence:** Fixed a compilation error and added debug logging to `loadConfig` to trace configuration loading and verify EEPROM persistence.

### Changed
- Standardized default axis mapping to AETR sequence (Roll, Pitch, Throttle, Yaw, AUX1, AUX2) for out-of-the-box compatibility.
- Updated Web Companion to match the new standard AETR-to-Gamepad axis sequence.

## [4.1.2-beta] — Stability & Optimization Update

### Fixed
- **Per-Axis Smoothing:** Fixed a bug where smoothing was only applied to the first axis due to incorrect delta-time calculation across sequential calls.
- **CRSF Transport Reliability:** Switched from PIO to Hardware UART (Serial1). This resolves ongoing link stability issues and provides a more robust data path for CRSF frames by leveraging RP2040's dedicated hardware controllers.
- **Mutex Stability:** Increased data mutex timeout from 2ms to 10ms to prevent transient link-loss reports during high CPU load.
- **CLI Safety:** Added length guarding to the CLI input buffer to prevent potential memory exhaustion from unbounded growth.
- **Safety Guards:** Added a minimum threshold for smoothing cutoff frequency to prevent division-by-zero errors.

### Changed
- **Web Companion Optimization:** Optimized mapping updates to use bulk JSON commands instead of multiple individual serial commands. This significantly reduces Flash memory wear (EEPROM) and improves UI responsiveness.

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
