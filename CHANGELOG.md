
### 5️⃣ **CHANGELOG.md** (Create if doesn't exist)

```markdown name=CHANGELOG.md
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2025-01-22

### Added
- **Persistent configuration storage** using ESP32 NVS (Non-Volatile Storage)
- UART settings now persist across reboots and power cycles
- Request/error counter tracking stored persistently
- Factory reset functionality via web UI and API
- Configuration section at top of code for easy customization
- OTA hostname and password configurable in code header
- Progress logging during OTA updates
- Better structured configuration constants

### Changed
- Moved OTA settings to configuration section (top of file)
- WiFi hostname now uses OTA_HOSTNAME for consistency
- Improved `initOTA()` function to use configuration constants
- Enhanced error messages in OTA callbacks

### Fixed
- Function declaration order (addLog forward declaration)
- UART config now saves immediately when changed
- Statistics persist across reboots

## [1.0.0] - 2025-01-21

### Added
- Initial release
- Modbus TCP to RTU bridge
- Embedded web interface
- REST API
- OTA updates
- Real-time logging
- Multiple function code support (01-06, 0F, 10)
- RS485 direction control (DE/RE)
- CRC validation
- Multiple simultaneous TCP connections

[1.1.0]: https://github.com/vwetter/esp32-modbus-gateway/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/vwetter/esp32-modbus-gateway/releases/tag/v1.0.0
