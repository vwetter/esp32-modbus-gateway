# ESP32 Modbus RTU ↔ TCP Gateway

![Version](https://img.shields.io/badge/version-1.4.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)
![Modbus](https://img.shields.io/badge/Modbus-RTU%20%7C%20TCP-orange.svg)
![Release](https://img.shields.io/github/v/release/vwetter/esp32-modbus-gateway)

Convert Modbus RTU (RS485) to Modbus TCP with a simple ESP32 board. Features **WiFi Manager** for easy deployment, **mobile-friendly** web interface, REST API, and OTA updates.

![Dashboard](docs/images/dashboard.png)

## 🎉 NEW in v1.4.0: Mobile-Optimized UI & Enhanced Device Support

### Mobile-First Design:
- ✨ **Responsive button layout** - Read/Write buttons side-by-side on mobile
- 📱 **Touch-friendly interface** - Optimized for smartphones and tablets
- 🎯 **Compact controls** - Better use of screen space

### Enhanced Modbus Compatibility:
- 🔧 **FC 0x10 Write Support** - Compatible with Deye inverters and similar devices
- ⚡ **Async Write Operations** - FreeRTOS tasks prevent ESP32 crashes
- 🕐 **Extended Timeouts** - 5-8 second timeouts for slow devices
- 📝 **Detailed Logging** - Full TX/RX packet inspection with timestamps

**Tested and verified with Deye solar inverters!**

## 🚀 NEW: Pure Arduino IDE Project (v1.2.3)

**Simplified development and deployment!** 

### Arduino IDE Ready:
1. **Pure Arduino IDE compatibility** - no more PlatformIO conflicts
2. **Single .ino file** - eliminates "multiple definition" errors  
3. **Easy setup** - detailed instructions in `ARDUINO_SETUP.md`
4. **Library documentation** - complete list in `libraries.txt`
5. **Streamlined structure** - clean project organization

### Smart WiFi Manager (v1.2.0):
1. **Flash generic firmware** - no code changes needed
2. **Smart AP-Mode**: `AP-modbus-gw` appears only when needed
3. **Web-based WiFi setup** - scan and select networks
4. **Clean operation** - AP automatically disabled after WiFi connection
5. **Intelligent fallback** - AP reappears if WiFi connection fails
6. **Persistent storage** - remembers settings after reboot

Perfect for **Arduino IDE users**, **mass deployment** and **field installations**!

![Dashboard](docs/images/dashboard.png)

## 🎯 What It Does

Bridges Modbus RTU devices (RS485) to Modbus TCP network, allowing you to:
- Connect legacy RS485 devices to modern SCADA/IoT systems
- Control industrial equipment via WiFi
- Monitor sensors remotely
- Integrate with Home Assistant, Node-RED, etc.

## 🛠 Hardware Needed

- ESP32 development board (~$8)
- MAX485 RS485 module (~$2)
- 6 jumper wires
- USB cable for programming

**Total: ~$15**

## 🔌 Wiring

```
ESP32          MAX485         RS485 Bus
─────          ──────         ─────────
3.3V    ───►   VCC
GND     ───►   GND
GPIO17  ───►   DI (TX)
GPIO16  ───►   RO (RX)
GPIO4   ───►   DE & RE
               A      ───►    A/+ (to device)
               B      ───►    B/- (to device)
```

💡 Add 120Ω resistor between A-B at both ends of RS485 bus

📸 [Detailed wiring diagram](hardware/wiring-diagram.png)

## ⚡ Quick Start

### Method 1: Smart WiFi Manager (Recommended) 🆕

1. **Flash the firmware** (see steps 1-3 below for Arduino IDE setup)
2. **Connect to AP**: Look for WiFi network `AP-modbus-gw` (password: `initpass`)
3. **Open browser**: Go to `http://192.168.4.1`
4. **Select your WiFi**: Choose network and enter password
5. **Clean operation**: AP automatically disappears after successful connection
6. **Done!** Device operates only on your WiFi network

### Method 2: Traditional (Edit Code)

For advanced users who prefer editing the source code directly.

### 1. Install Arduino IDE

Download from [arduino.cc](https://www.arduino.cc/en/software)

📋 **Detailed setup instructions in [`ARDUINO_SETUP.md`](ARDUINO_SETUP.md)**

### 2. Add ESP32 Support

- Go to **File → Preferences**
- Add to "Additional Board Manager URLs":
  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```
- Open **Tools → Board → Boards Manager**
- Install "ESP32 by Espressif Systems"

### 3. Install Libraries

Open **Tools → Manage Libraries**, install:
- **AsyncTCP** by dvarrel
- **ESP Async WebServer** by lacamera
- **ArduinoJson** (version 6.x) by Benoit Blanchon

📋 **Complete library list in [`libraries.txt`](libraries.txt)**

### 4. Upload Firmware

1. Open `esp32-modbus-gateway.ino`
2. Select Board: **Tools → Board → ESP32 Dev Module**
3. Select Port: **Tools → Port → (your COM port)**
4. Click **Upload** ⬆️

### 5. Configure WiFi

**Option A: WiFi Manager (Easy)**
1. Connect to `AP-modbus-gw` WiFi (password: `initpass`)
2. Browser opens automatically to `http://192.168.4.1`
3. Select your WiFi network and enter password
4. Save & reboot

**Option B: Traditional (Expert)**
1. Edit lines 23-24 in code before uploading:
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASS = "YourPassword";
   ```

### 6. Find Your Gateway

**WiFi Manager**: Check your router's DHCP list or use network scanner

**Traditional**: Open Serial Monitor (115200 baud) to see IP address

## 🌐 Web Interface

### Main Dashboard (Normal Operation)
Open `http://[gateway-ip]` in your browser to:
- View real-time statistics
- Test Modbus read/write
- Change UART settings (baud rate, parity, etc.)
- View live logs
- **Reset WiFi configuration** (return to AP mode)

![Web UI](docs/images/dashboard.png)

### WiFi Setup Interface (First Boot / Reconfiguration)
When device needs WiFi configuration:
- **Automatic AP mode**: `AP-modbus-gw` appears automatically
- **WiFi network scanning** with signal strength
- **Point-and-click** network selection
- **Secure password entry**
- **Clean shutdown** - AP disappears after successful connection

Perfect for **field installations** without laptops and **clean production networks**!

## 📡 Using the API

### Read Registers

```bash
curl -X POST http://10.0.0.46/api/modbus/read \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1, "address":0, "quantity":10}'
```

### Write Register

```bash
curl -X POST http://10.0.0.46/api/modbus/write \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1, "address":100, "value":1234}'
```

### Get Status

```bash
curl http://10.0.0.46/api/status
```

📖 [Full API Documentation](docs/API.md)

## 🐍 Python Example

```python
import requests

gateway = "http://10.0.0.46"

# Read 10 registers
response = requests.post(f"{gateway}/api/modbus/read", json={
    "slave_id": 1,
    "address": 0,
    "quantity": 10
})

if response.json()["success"]:
    print(f"Values: {response.json()['values']}")
```

More examples in [`examples/`](examples/)

## ⚙️ Configuration

### WiFi Manager Configuration (Recommended)

No code editing needed! All done via web interface:

1. **Access Point Settings** (when unconfigured):
   - SSID: `AP-modbus-gw`  
   - Password: `initpass`
   - IP: `192.168.4.1`

2. **Web Configuration**: Automatic WiFi scanning and setup
3. **Persistent Storage**: Settings saved to ESP32 flash memory
4. **Easy Reset**: "Reset WiFi Config" button in web interface

### Traditional Configuration (Expert Mode)

All settings in the `.ino` file (only needed if not using WiFi Manager):

```cpp
// WiFi Settings (legacy - now stored in NVS)
const char* AP_SSID      = "AP-modbus-gw";
const char* AP_PASS      = "initpass";

// OTA Settings
const char* OTA_HOSTNAME = "esp32-modbus-gw";
const char* OTA_PASSWORD = "esphome123";  // ⚠️ Change for production!

// Hardware Pins
#define MODBUS_TX_PIN     17
#define MODBUS_RX_PIN     16
#define MODBUS_DE_PIN     4

// Serial (used on first boot only)
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_DATABITS 8
#define MODBUS_DEFAULT_PARITY   'N'  // N, E, or O
#define MODBUS_DEFAULT_STOPBITS 1

// Network Ports
#define WEB_PORT        80
#define MODBUS_TCP_PORT 502
```

## 🔄 WiFi Management

### Deployment Workflow

1. **Flash once**: Upload generic firmware to all devices
2. **Deploy anywhere**: No pre-configuration needed
3. **On-site setup**: Connect to `AP-modbus-gw`, configure WiFi via browser
4. **Clean operation**: AP automatically disappears after WiFi connection
5. **Automatic fallback**: AP reappears if WiFi connection fails
6. **Easy reconfiguration**: Reset WiFi anytime via web interface

### Smart AP Behavior

The gateway uses intelligent AP management:

| Situation | AP Status | WiFi Status | Behavior |
|-----------|-----------|-------------|----------|
| **First boot** | ✅ Active | ❌ Disconnected | Setup via `AP-modbus-gw` |
| **WiFi configured** | ❌ **Auto-disabled** | ✅ Connected | Clean operation, no AP pollution |
| **WiFi fails** | ✅ **Auto-enabled** | ❌ Disconnected | Fallback for reconfiguration |
| **WiFi reset** | ✅ Active | ❌ Disconnected | Ready for new configuration |

### WiFi Reset Options

- **Soft Reset**: Via web interface → "Reset WiFi Config" button
- **Factory Reset**: Via web interface → "Factory Reset" button  
- **Hard Reset**: Clear all NVS data, return to defaults

### Fallback Mechanisms

- **Connection Failed**: Automatically enables AP mode for reconfiguration
- **Network Changed**: Reset WiFi config to connect to new network
- **Password Changed**: Use WiFi reset to update credentials

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| **Can't find WiFi setup** | Look for `AP-modbus-gw` network, password: `initpass` |
| **WiFi setup page won't load** | Try `http://192.168.4.1` directly |
| **Device won't connect to WiFi** | Check password, use 2.4GHz network only |
| **Lost WiFi after router change** | AP automatically appears for reconfiguration |
| **Can't access after WiFi config** | Check router DHCP list for device IP |
| **AP still visible after setup** | Normal - AP disappears after successful WiFi connection |
| **Need to reconfigure WiFi** | Use "Reset WiFi Config" button in web interface |
| **No Modbus response** | Verify wiring, check slave address and baud rate |
| **"CRC error" in logs** | Add/check 120Ω termination resistors |
| **Random crashes** | Check power supply (needs stable 5V 1A) |

### WiFi Manager Troubleshooting

1. **Can't see AP network**: 
   - Wait 30 seconds after power-on
   - Check if device already configured (look for existing connection)
   - Factory reset if needed

2. **AP mode not working**:
   - Ensure device is unconfigured or reset
   - Check 2.4GHz WiFi capability on your phone/laptop

3. **Configuration not saving**:
   - Ensure stable power supply during setup
   - Wait for automatic reboot before disconnecting

📖 [Detailed troubleshooting](docs/INSTALLATION.md#troubleshooting)

## 📚 Documentation

- [Installation Guide](docs/INSTALLATION.md) - Detailed setup instructions
- [Wiring Guide](docs/WIRING.md) - RS485 connection details
- [API Reference](docs/API.md) - Complete REST API documentation

## ✨ Features

- ✅ **Smart WiFi Manager** - Intelligent AP mode with auto-disable
- ✅ **Mobile-Friendly UI** - Responsive design for smartphones and tablets
- ✅ **FC 0x10 Write Support** - Compatible with Deye inverters and similar devices
- ✅ **Async Operations** - FreeRTOS tasks prevent blocking and crashes
- ✅ **Extended Timeouts** - Up to 8 seconds for slow devices
- ✅ **Generic Firmware** - Flash once, configure anywhere  
- ✅ **Clean Operation** - AP automatically hidden after WiFi connection
- ✅ **Auto Fallback** - AP reappears automatically when WiFi fails
- ✅ **Persistent Config** - Settings saved to NVS flash memory
- ✅ **WiFi Reset** - Easy reconfiguration via web interface
- ✅ Modbus TCP server (port 502)
- ✅ Modbus RTU master (RS485)
- ✅ Web interface (embedded, no SPIFFS needed)
- ✅ REST API for automation
- ✅ OTA firmware updates
- ✅ Real-time logs with packet inspection
- ✅ Runtime UART configuration
- ✅ Multiple simultaneous TCP connections
- ✅ Automatic RS485 direction control
- ✅ CRC validation with detailed error reporting
- ✅ Function codes: 01, 02, 03, 04, 05, 06, 0F, **10** (Write Multiple Registers)

## � Use Cases

### Industrial Applications
- **Factory automation** - Connect legacy PLCs to modern SCADA systems
- **Remote monitoring** - Monitor industrial sensors over WiFi
- **Equipment integration** - Bridge old RS485 devices to IoT platforms
- **Energy management** - Connect power meters to monitoring systems

### Smart Building/Home
- **HVAC control** - Integrate building automation systems
- **Energy monitoring** - Connect smart meters to home automation
- **Lighting control** - Bridge Modbus lighting controllers
- **Security systems** - Integrate access control devices

### Deployment Advantages
- **Mass deployment** - Flash identical firmware to hundreds of devices
- **Field installation** - No laptop needed for WiFi configuration
- **Clean networks** - No permanent AP pollution in production
- **Maintenance-friendly** - Easy WiFi reconfiguration via web interface
- **Cost-effective** - ~$15 hardware cost per gateway
- **Robust operation** - Automatic fallback and recovery mechanisms

## 🏠 Integrations

Works seamlessly with:
- **Home Assistant** - [Example config](examples/home-assistant/)
- **Node-RED** - Direct HTTP nodes
- **Grafana/InfluxDB** - Via Python scripts
- **Any SCADA** - Standard Modbus TCP
- **Python/Node.js** - REST API examples included

## 🤝 Contributing

Pull requests welcome! Please:
1. Test your changes thoroughly
2. Update documentation if needed
3. Follow the existing code style

## 📄 License

MIT License - see [LICENSE](LICENSE) file

## 🙏 Credits

Built with:
- [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) by me-no-dev
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) by me-no-dev
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon
- .... and obviously Github Copilot

## ⭐ Support

If this helped you, please ⭐ star the repo!

**Questions?** Open an [issue](https://github.com/vwetter/esp32-modbus-gateway/issues)

---

**Made by [@vwetter](https://github.com/vwetter)** • **2025-11-06** • **v1.4.0 - Mobile-Optimized & Deye Compatible**
