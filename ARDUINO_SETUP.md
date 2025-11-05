Arduino IDE Setup Guide
========================

This project is now a pure Arduino IDE project. Follow these steps to get started:

## 1. Install Arduino IDE

Download and install Arduino IDE 2.x from:
https://www.arduino.cc/en/software

## 2. Add ESP32 Board Support

1. Open Arduino IDE
2. Go to File → Preferences
3. Add this URL to "Additional Board Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to Tools → Board → Boards Manager
5. Search for "ESP32" and install "ESP32 by Espressif Systems"

## 3. Install Required Libraries

Go to Tools → Manage Libraries and install:

1. **AsyncTCP** by dvarrel
   - Search: "AsyncTCP"
   - Install the version by dvarrel

2. **ESP Async WebServer** by lacamera
   - Search: "ESP Async WebServer" 
   - Install the version by lacamera

3. **ArduinoJson** by Benoit Blanchon
   - Search: "ArduinoJson"
   - Install version 6.x (NOT 7.x)

## 4. Board Configuration

Select these settings in Tools menu:

- **Board**: "ESP32 Dev Module"
- **Upload Speed**: 921600
- **CPU Frequency**: 240MHz (WiFi/BT)
- **Flash Frequency**: 80MHz
- **Flash Mode**: QIO
- **Flash Size**: 4MB (32Mb)
- **Partition Scheme**: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
- **Core Debug Level**: "None"
- **PSRAM**: "Disabled"

## 5. Upload Firmware

1. Open `esp32-modbus-gateway.ino`
2. Connect your ESP32 via USB
3. Select the correct COM port in Tools → Port
4. Click Upload (Ctrl+U)

## 6. Verify Installation

1. Open Serial Monitor (Tools → Serial Monitor)
2. Set baud rate to 115200
3. Press reset button on ESP32
4. You should see boot messages and WiFi setup instructions

## Project Structure

```
esp32-modbus-gateway/
├── esp32-modbus-gateway.ino    # Main Arduino sketch
├── libraries.txt               # Required libraries list
├── README.md                   # Project documentation
├── docs/                       # Documentation
├── examples/                   # Usage examples
└── hardware/                   # Hardware documentation
```

## Troubleshooting

**"Library not found" errors:**
- Make sure all 3 libraries are installed correctly
- Restart Arduino IDE after installing libraries

**"Board not found" errors:**
- Make sure ESP32 board package is installed
- Try different USB cable/port
- Press and hold BOOT button during upload if needed

**Compilation errors:**
- Make sure you're using ArduinoJson version 6.x (not 7.x)
- Check that all libraries are the correct versions

## Why Arduino IDE Only?

This project was converted from PlatformIO to pure Arduino IDE to:
- Eliminate "multiple definition" compilation errors
- Avoid inconsistencies between .ino and .cpp files
- Simplify deployment and distribution
- Make it accessible to more users
- Reduce maintenance overhead

For advanced users who prefer PlatformIO, you can easily convert this back by creating a src/main.cpp file with the .ino content.