# WiFi Manager Guide

The ESP32 Modbus Gateway includes a built-in WiFi Manager that eliminates the need to hardcode WiFi credentials in the firmware. This enables **generic deployments** where the same firmware can be flashed to multiple devices and configured on-site.

## 🎯 Overview

### Traditional Method Problems
- ❌ WiFi credentials hardcoded in source code
- ❌ Must recompile firmware for each deployment location  
- ❌ Requires development environment for configuration
- ❌ Difficult to change networks after deployment

### WiFi Manager Solution
- ✅ **Generic firmware** - flash once, configure anywhere
- ✅ **Web-based setup** - no coding or recompilation needed
- ✅ **Field-friendly** - configure with just a smartphone
- ✅ **Persistent storage** - remembers settings permanently
- ✅ **Easy reconfiguration** - change networks anytime

## 🚀 Quick Start

### Step 1: Flash Generic Firmware
1. Download and flash `esp32-modbus-gateway.ino` 
2. No code editing required!
3. Device will boot in setup mode

### Step 2: Connect to Setup Network
1. Power on the ESP32
2. Wait 30 seconds for boot
3. Look for WiFi network: **`AP-modbus-gw`**
4. Connect with password: **`initpass`**

### Step 3: Configure WiFi
1. Browser should open automatically to setup page
2. If not, navigate to: `http://192.168.4.1`
3. Click "Scan Networks" to see available WiFi
4. Select your network from the list
5. Enter your WiFi password
6. Click "Save & Connect"

### Step 4: Normal Operation
1. Device reboots automatically
2. Connects to your configured WiFi network
3. Access normal web interface via device IP
4. Check router's DHCP list for the IP address

## 🔧 Advanced Configuration

### Access Point Settings

When unconfigured, the device creates its own WiFi network:

- **Network Name (SSID)**: `AP-modbus-gw`
- **Password**: `initpass`
- **IP Address**: `192.168.4.1`
- **Web Interface**: `http://192.168.4.1`

### Customizing AP Settings

To change the default AP settings, edit these lines in the source code:

```cpp
// WiFi Settings  
const char* AP_SSID = "AP-modbus-gw";      // Change AP network name
const char* AP_PASS = "initpass";          // Change AP password
```

### Storage Location

WiFi credentials are stored in ESP32's **Non-Volatile Storage (NVS)**:
- Survives power cycles and reboots
- Separate from firmware (not erased during updates)
- Can be cleared via web interface or factory reset

## 🌐 Web Interface Features

### Setup Mode Interface

When device is unconfigured or in AP mode:

1. **Network Scanning**
   - Automatically scans for available WiFi networks
   - Shows signal strength (RSSI) 
   - Indicates security status (open/encrypted)
   - Click any network to auto-fill SSID

2. **Manual Entry**
   - Type SSID manually for hidden networks
   - Password field with security masking
   - Form validation before submission

3. **Status Feedback**
   - Real-time setup progress
   - Success/error notifications
   - Automatic reboot countdown

### Normal Operation Interface

Once WiFi is configured:

1. **WiFi Status Display**
   - Current network name
   - IP address and signal strength
   - Connection status indicator

2. **WiFi Management**
   - "Reset WiFi Config" button
   - Returns device to AP mode for reconfiguration
   - Requires confirmation to prevent accidents

## 🔄 WiFi Management Operations

### Reconfiguring WiFi

To change to a different WiFi network:

1. **Via Web Interface** (Recommended):
   - Open normal web interface
   - Go to "UART Config" tab
   - Click "Reset WiFi Config" 
   - Device reboots in AP mode
   - Follow setup steps again

2. **Via Factory Reset**:
   - Clears all settings including WiFi
   - Returns to completely fresh state

### Troubleshooting Connection Issues

If WiFi connection fails:

1. **Automatic Fallback**:
   - Device automatically enables AP mode
   - Connect to `AP-modbus-gw` to reconfigure

2. **Manual Reset**:
   - Use "Reset WiFi Config" button
   - Clear credentials and try again

3. **Check Common Issues**:
   - Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
   - Verify password accuracy
   - Check network availability and signal strength

## 📱 Mobile-Friendly Setup

The setup interface is optimized for smartphones:

- **Responsive design** - works on any screen size
- **Touch-friendly** - large buttons and form fields
- **Auto-scaling** - readable text on mobile devices
- **Captive portal** - some phones auto-open the setup page

### iOS Setup
1. Connect to `AP-modbus-gw` network
2. iOS may show "No Internet" warning - ignore it
3. Tap notification or open Safari to `192.168.4.1`

### Android Setup  
1. Connect to `AP-modbus-gw` network
2. Android may prompt to "Sign in to network"
3. Tap notification or open browser to `192.168.4.1`

## 🏭 Mass Deployment Workflow

For deploying many gateways:

### Preparation Phase
1. **Prepare firmware**: Flash identical firmware to all devices
2. **No configuration needed**: All devices boot in AP mode
3. **Label devices**: Track MAC addresses if needed

### Deployment Phase
1. **Install hardware**: Mount and wire each gateway
2. **Power on**: Device creates `AP-modbus-gw` network
3. **Connect smartphone**: Join AP network with `initpass`
4. **Configure WiFi**: Select local network and enter password
5. **Verify operation**: Check device appears on local network

### Maintenance Phase
1. **Network changes**: Use "Reset WiFi Config" to reconfigure
2. **Firmware updates**: WiFi settings preserved during OTA updates
3. **Factory reset**: Available for complete reset when needed

## 🔐 Security Considerations

### Default Credentials
- **Change AP password**: Edit `AP_PASS` for production deployments
- **Secure local networks**: Use WPA2/WPA3 for target WiFi networks
- **Physical access**: Setup mode requires physical proximity

### Network Security
- Device only stores WiFi credentials locally
- No cloud connectivity for setup process
- All configuration happens on local network

### Production Recommendations
1. **Change default AP password** before mass deployment
2. **Use dedicated IoT network** for gateway devices  
3. **Monitor device connections** via network management
4. **Regular firmware updates** via OTA

## 🛠️ API Reference

### WiFi Management Endpoints

The following API endpoints are available for WiFi management:

#### Scan Networks
```http
GET /api/wifi/scan
```

Response:
```json
{
  "networks": [
    {
      "ssid": "MyNetwork",
      "rssi": -45,
      "encryption": true
    }
  ]
}
```

#### Configure WiFi
```http
POST /api/wifi/config
Content-Type: application/json

{
  "ssid": "MyNetwork",
  "password": "MyPassword"
}
```

#### Reset WiFi Configuration
```http
POST /api/wifi/reset
```

### Status Information

The status endpoint includes WiFi configuration info:

```http
GET /api/status
```

Response includes:
```json
{
  "wifi_connected": true,
  "wifi_configured": true,
  "wifi_ssid": "MyNetwork",
  "wifi_ip": "192.168.1.100"
}
```

## 🐛 Troubleshooting

### Common Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Can't see AP network | Device already configured | Check if device connected to existing WiFi |
| Setup page won't load | Wrong IP address | Try `http://192.168.4.1` directly |
| WiFi scan shows no networks | 5GHz only environment | Use 2.4GHz network or mobile hotspot |
| Connection fails after setup | Wrong password | Use WiFi reset to try again |
| Lost device after WiFi change | Network unavailable | Device will enable AP mode automatically |

### Debug Information

Enable debug output in Serial Monitor (115200 baud):

```
WiFi config loaded: MyNetwork
Attempting WiFi connection to: MyNetwork
WiFi: MyNetwork IP: 192.168.1.100
```

### Factory Reset Options

1. **WiFi Only**: "Reset WiFi Config" button (keeps other settings)
2. **Complete**: "Factory Reset" button (erases everything)
3. **Code-based**: Call `clearWifiConfig()` in firmware

## 📚 Related Documentation

- [Main README](../README.md) - Overview and quick start
- [Installation Guide](INSTALLATION.md) - Detailed setup instructions  
- [API Reference](API.md) - Complete REST API documentation
- [Hardware Guide](WIRING.md) - RS485 wiring and connections

---

**Need help?** Open an [issue](https://github.com/vwetter/esp32-modbus-gateway/issues) with "WiFi Manager" in the title.