# Installation Guide

## Prerequisites

- Windows, Mac, or Linux computer
- USB cable
- Internet connection

## Step 1: Install Arduino IDE

1. Download from https://www.arduino.cc/en/software
2. Install and launch

## Step 2: Add ESP32 Support

1. Open **File → Preferences**
2. In "Additional Board Manager URLs", paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Click **OK**
4. Go to **Tools → Board → Boards Manager**
5. Search "ESP32"
6. Install "**ESP32 by Espressif Systems**" (version 2.0.14 or newer)

## Step 3: Install Libraries

1. Open **Tools → Manage Libraries**
2. Search and install each:

| Library Name | Author | Version |
|--------------|--------|---------|
| AsyncTCP | me-no-dev | Latest |
| ESPAsyncWebServer | me-no-dev | Latest |
| ArduinoJson | Benoit Blanchon | 6.x |

## Step 4: Wire the Hardware

See [WIRING.md](WIRING.md) for detailed instructions.

**Quick reference:**
```
ESP32 → MAX485
3.3V  → VCC
GND   → GND
GPIO17 → DI
GPIO16 → RO
GPIO4  → DE & RE (tie together)
```

## Step 5: Configure Firmware

1. Download/clone this repository
2. Open `esp32-modbus-gateway.ino` in Arduino IDE
3. Edit lines 23-24:
   ```cpp
   const char* WIFI_SSID = "YourWiFiName";
   const char* WIFI_PASS = "YourPassword";
   ```

## Step 6: Upload

1. Connect ESP32 to computer via USB
2. **Tools → Board** → Select "**ESP32 Dev Module**"
3. **Tools → Port** → Select your COM/USB port
4. Set these if needed:
   - Upload Speed: 921600
   - Flash Frequency: 80MHz
   - Partition Scheme: Default 4MB
5. Click **Upload** button (→)
6. Wait for "Hard resetting via RTS pin..."

## Step 7: Verify

1. Open **Tools → Serial Monitor**
2. Set baud rate to **115200**
3. Press ESP32 reset button
4. You should see:
   ```
   ========================================
   ESP32 Modbus Gateway
   ========================================
   [0s] [info] UART started: 9600 8N1
   [2s] [success] Connected to WiFi: YourNetwork IP: 10.0.0.46
   [2s] [info] Web server started
   ```

## Troubleshooting

### Upload Fails

**"Failed to connect"**
- Press and hold BOOT button on ESP32 during upload
- Try different USB cable (must support data)
- Install CP210x or CH340 drivers

**"Port not found"**
- Check Device Manager (Windows) / System Report (Mac)
- Install USB-Serial drivers
- Try different USB port

### Won't Connect to WiFi

**Check:**
- SSID and password are correct (case-sensitive)
- Router uses 2.4GHz (ESP32 doesn't support 5GHz)
- Router DHCP is enabled
- Router doesn't block new devices

**Fallback:**
If WiFi fails, gateway creates its own network:
- SSID: `ESP32-ModbusGW`
- Password: `12345678`
- IP: `192.168.4.1`

### Web Interface Doesn't Load

- Check IP address in Serial Monitor
- Try `http://esp32-modbus-gateway.local`
- Clear browser cache (Ctrl+Shift+R)
- Disable VPN

### No Modbus Communication

See [Troubleshooting](../README.md#troubleshooting) section in main README

## Next Steps

- [Configure RS485 wiring](WIRING.md)
- [Test API](API.md)
- Check [examples](../examples/)
