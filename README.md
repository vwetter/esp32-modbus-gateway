# ESP32 Modbus RTU ↔ TCP Gateway

![Version](https://img.shields.io/badge/version-1.1.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-lightgrey.svg)
![Modbus](https://img.shields.io/badge/Modbus-RTU%20%7C%20TCP-orange.svg)
![Release](https://img.shields.io/github/v/release/vwetter/esp32-modbus-gateway)

Convert Modbus RTU (RS485) to Modbus TCP with a simple ESP32 board. Includes web interface, REST API, and OTA updates.

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

### 1. Install Arduino IDE

Download from [arduino.cc](https://www.arduino.cc/en/software)

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
- **AsyncTCP** by me-no-dev
- **ESPAsyncWebServer** by me-no-dev  
- **ArduinoJson** (version 6.x) by Benoit Blanchon

### 4. Configure & Upload

1. Open `esp32-modbus-gateway.ino`
2. Edit WiFi credentials (lines 23-24):
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASS = "YourPassword";
   ```
3. Select Board: **Tools → Board → ESP32 Dev Module**
4. Select Port: **Tools → Port → (your COM port)**
5. Click **Upload** ⬆️

### 5. Find Your Gateway

Open Serial Monitor (115200 baud). After boot, you'll see:
```
Connected to WiFi: YourNetwork IP: 10.0.0.46
Web interface at: http://10.0.0.46
```

## 🌐 Web Interface

Open `http://[gateway-ip]` in your browser to:
- View real-time statistics
- Test Modbus read/write
- Change UART settings (baud rate, parity, etc.)
- View live logs

![Web UI](docs/images/dashboard.png)

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

All settings in the `.ino` file:

```cpp
// WiFi Settings
const char* WIFI_SSID    = "YOUR_SSID";
const char* WIFI_PASS    = "YOUR_PASSWORD";
const char* AP_SSID      = "ESP32-ModbusGW";
const char* AP_PASS      = "12345678";

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

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| **Won't connect to WiFi** | Check SSID/password, use 2.4GHz network |
| **No Modbus response** | Verify wiring, check slave address and baud rate |
| **"CRC error" in logs** | Add/check 120Ω termination resistors |
| **Random crashes** | Check power supply (needs stable 5V 1A) |

📖 [Detailed troubleshooting](docs/INSTALLATION.md#troubleshooting)

## 📚 Documentation

- [Installation Guide](docs/INSTALLATION.md) - Detailed setup instructions
- [Wiring Guide](docs/WIRING.md) - RS485 connection details
- [API Reference](docs/API.md) - Complete REST API documentation

## ✨ Features

- ✅ Modbus TCP server (port 502)
- ✅ Modbus RTU master (RS485)
- ✅ Web interface (embedded, no SPIFFS needed)
- ✅ **Persistent configuration** (settings saved to NVS flash)
- ✅ REST API for automation
- ✅ OTA firmware updates
- ✅ Real-time logs
- ✅ Runtime UART configuration
- ✅ Multiple simultaneous TCP connections
- ✅ Automatic RS485 direction control
- ✅ CRC validation
- ✅ Function codes: 01, 02, 03, 04, 05, 06, 0F, 10

## 🏠 Integrations

Works with:
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

**Made by [@vwetter](https://github.com/vwetter)** • **2025-01-21**
