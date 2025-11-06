
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4.0] - 2025-11-06

### Added
- **Mobile-friendly button layout** - Read/Write buttons now side-by-side with flexbox
- Shortened button labels ("Read Regs" / "Write Reg") for better mobile UX
- Responsive button group styling with proper wrapping

### Changed
- **Write operations now use FC 0x10** (Write Multiple Registers) instead of FC 0x06
- Improved compatibility with Deye inverters and similar devices that only accept FC 0x10
- Button layout optimized for touch interfaces and small screens

### Fixed
- Button layout issues on mobile devices
- Write timeout issues with slow Modbus devices (Deye inverters)
- ESP32 crashes during write operations

### Tested
- Successfully verified with Deye solar inverter
- All write operations working correctly with FC 0x10 (137ms response time)
- Mobile UI tested on various screen sizes

## [1.3.3] - 2025-11-06

### Changed
- Switched to FC 0x10 (Write Multiple Registers) for all write operations
- Some devices only accept FC 0x10 even for single register writes

### Fixed
- Deye inverter compatibility - writes now working with FC 0x10

## [1.3.2] - 2025-11-06

### Added
- `/api/errors/reset` endpoint for resetting error counter
- **FreeRTOS task for async Modbus writes** - prevents blocking and crashes
- JSON parse validation in all API endpoints
- HTTP 202 Accepted response for async write operations

### Fixed
- **CRITICAL:** Removed static String variables in async callbacks (caused ESP32 crashes)
- Memory corruption issues in Write operations
- ESP32 connection reset errors during write operations (`net::ERR_CONNECTION_RESET`)

### Changed
- Write operations now execute in separate FreeRTOS task (non-blocking)
- Improved error handling with proper JSON validation throughout

## [1.3.1] - 2025-11-06

### Added
- Console.log debugging for Write operations in browser
- Improved error handling in modbusWrite() JavaScript function

### Fixed
- Better error messages in browser console for debugging

## [1.3.0] - 2025-11-06

### Added
- **Modbus Write support** with extended timeout for slow devices (Deye inverters)
- Detailed write logging with timestamps and duration
- WiFi setup page for initial configuration in AP mode
- Serial log buffer for UI display
- Serial Log tab with auto-refresh (2 seconds)
- Firmware version display in UI header
- Dynamic version loading from API

### Changed
- Extended RTU timeout from 1000ms to 2000ms for reads
- Added 5000ms + 3000ms timeout for write operations (total 8s)
- Memory optimizations for large operations

### Fixed
- Boot loop issues with yield() calls and delays
- Memory overflow from stack arrays (switched to malloc/free)

## [1.2.3] - 2025-11-05

### Changed
- **Converted to pure Arduino IDE project** - Eliminates compilation conflicts
- **Removed PlatformIO structure** - No more `src/`, `lib/`, `include/`, `platformio.ini`
- **Single source of truth** - Only `esp32-modbus-gateway.ino` file remains
- **Simplified deployment** - Direct Arduino IDE compatibility

### Added
- `ARDUINO_SETUP.md` - Detailed Arduino IDE setup instructions
- `libraries.txt` - Complete list of required libraries with versions
- Enhanced project documentation for Arduino IDE users

### Fixed
- **"Multiple definition" compilation errors** - Caused by duplicate code in .ino and src/main.cpp
- Arduino IDE compatibility issues
- Inconsistencies between PlatformIO and Arduino IDE builds

### Removed
- PlatformIO configuration files and directory structure
- Duplicate code sources
- Build system complexity

## [1.2.2] - 2025-11-01

### Added
- **Error Counter Reset** - New "Reset Errors" button in Web UI statistics card
- HTTP API endpoint `POST /api/errors/reset` to reset error counter (persists to NVS)
- Improved Modbus timing for better evcc compatibility

### Changed
- Code cleanup - removed temporary debug outputs
- Enhanced MAX485 timing:
  - Increased DE/RE switch delays (before and after TX: 100µs → 500µs)
  - Added critical 2ms delay after `setDE(false)` for RX mode switching
  - More thorough UART buffer clearing before transmission
  - Added 75ms post-transaction delay for rapid request stability
- Increased Modbus RTU RX timeout from 1000ms to 2000ms
- Increased inter-frame delay from 10ms to 50ms

### Fixed
- **"No response" errors with evcc** - resolved timing issues with rapid Modbus polling
- MAX485 RX/TX mode switching race condition
- Improved reliability for consecutive Modbus requests

### Commits
- 3cc2fae — Add HTTP POST handler for /api/errors/reset
- a2772a1 — Update esp32-modbus-gateway.ino with UI improvements
- e38d542 — Code cleanup and version bump


## [1.2.0] - 2025-01-27

### Added
- **🚀 WiFi Manager functionality** - Major feature for generic deployments
- Auto Access Point mode on first boot (`AP-modbus-gw`, password: `initpass`)
- Web-based WiFi configuration interface with network scanning
- WiFi credentials stored in NVS (Non-Volatile Storage)
- WiFi reset functionality via web interface
- Automatic fallback to AP mode when WiFi connection fails
- Mobile-friendly setup interface with responsive design
- WiFi network scanning with RSSI and encryption status
- Generic firmware deployment (no code changes needed for different networks)
- Enhanced status API with WiFi configuration information

### Changed
- **BREAKING**: Removed hardcoded WiFi credentials (`WIFI_SSID`, `WIFI_PASS`)
- Access Point SSID changed to `AP-modbus-gw` (was `ESP32-ModbusGW`)
- Access Point password changed to `initpass` (was `12345678`)
- Boot sequence now checks for saved WiFi configuration
- Web interface serves different pages based on configuration state
- Factory reset now also clears WiFi configuration
- Enhanced troubleshooting documentation

### Fixed
- Improved WiFi connection reliability with timeout handling
- Better error handling for WiFi setup process
- Enhanced logging for WiFi connection states

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

[1.2.2]: https://github.com/vwetter/esp32-modbus-gateway/compare/v1.2.0...v1.2.2
[1.2.0]: https://github.com/vwetter/esp32-modbus-gateway/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/vwetter/esp32-modbus-gateway/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/vwetter/esp32-modbus-gateway/releases/tag/v1.0.0
