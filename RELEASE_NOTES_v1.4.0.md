# Release Notes - v1.4.0

**Release Date:** November 6, 2025

## 🎉 Major Improvements

### Mobile-Optimized User Interface
The web interface has been redesigned with mobile devices in mind:
- **Side-by-side buttons** - Read and Write buttons now appear next to each other instead of stacked
- **Responsive layout** - Flexbox-based button groups that adapt to screen size
- **Compact labels** - Shortened to "Read Regs" and "Write Reg" for better fit on small screens
- **Touch-friendly** - Proper spacing and sizing for finger-based interaction

Perfect for **field technicians** using smartphones to configure devices!

### Enhanced Modbus Device Compatibility

#### FC 0x10 Write Support (Deye & Similar Devices)
- **Function Code 0x10** (Write Multiple Registers) is now used for all write operations
- Resolves compatibility issues with devices that only accept FC 0x10:
  - ✅ **Deye Solar Inverters** (tested and verified)
  - ✅ Growatt Inverters
  - ✅ Other restrictive Modbus implementations

**Technical Details:**
```
Old (FC 0x06): 00 06 01 54 2A F8 [CRC]           - 8 bytes
New (FC 0x10): 00 10 01 54 00 01 02 2A F8 [CRC]  - 11 bytes

Response time: ~137ms (tested with Deye)
```

### Improved Stability

#### Async Write Operations
- **FreeRTOS Tasks** - Write operations run in separate tasks
- **No more crashes** - Prevents ESP32 resets during long operations
- **HTTP 202 Accepted** - Immediate response to client, operation continues in background
- **Extended timeouts** - Up to 8 seconds for slow devices

## 🔧 Technical Changes

### Code Improvements
- Removed static String variables from async callbacks (memory safety)
- Added JSON parse validation to all API endpoints
- Improved error handling and reporting
- Better memory management in web server callbacks

### UI Enhancements
- New `.button-group` CSS class with flexbox
- Responsive button sizing with `flex: 1` and `min-width: 140px`
- Removed hardcoded margins, using gap instead
- Better mobile breakpoint handling

## 📊 Testing Results

### Verified Configurations
- ✅ **Deye Solar Inverter** - Full read/write functionality
  - Register 340 write test: 11000 (0x2AF8) - Success in 137ms
  - Register 340 read verification: Confirmed value written correctly
- ✅ **Mobile browsers** - iPhone, Android, tablets
- ✅ **Desktop browsers** - Chrome, Firefox, Edge, Safari

### Performance Metrics
- Write operation latency: 137ms (Deye inverter @ 9600 baud)
- Read operation latency: ~30-50ms
- ESP32 memory usage: ~186KB free heap
- Task stack: 4096 bytes per write task

## 🐛 Bug Fixes

### Critical Fixes
- **ESP32 crashes during write** - Fixed by implementing FreeRTOS tasks
- **Connection reset errors** - Resolved by async operation handling
- **Memory corruption** - Eliminated static String usage in callbacks

### UI Fixes
- **Mobile button layout** - Buttons no longer overflow on small screens
- **Touch target size** - Improved accessibility on touch devices

## 📚 Documentation Updates

### Updated Files
- `README.md` - New v1.4.0 features section
- `CHANGELOG.md` - Complete version history from v1.3.0 to v1.4.0
- Release notes (this file)

### New Documentation
- Mobile UI design decisions
- FC 0x10 compatibility notes
- Deye inverter testing results

## 🚀 Upgrade Instructions

### From v1.3.x
1. Flash new firmware via OTA or USB
2. No configuration changes needed - all settings preserved
3. Enjoy improved mobile UI and Deye compatibility!

### From v1.2.x or earlier
1. Flash new firmware
2. Use WiFi Manager to reconfigure if needed
3. Test write operations with your Modbus devices
4. Check Serial Log tab for detailed operation logs

## 🔄 Breaking Changes

**None!** This release is fully backward compatible.

## ⚠️ Known Issues

None reported. Please open an issue on GitHub if you encounter problems.

## 🎯 Use Cases Now Supported

### Solar Energy Systems
- ✅ Deye solar inverters - full read/write control
- ✅ Remote monitoring and configuration
- ✅ Battery management system integration

### Mobile Field Work
- ✅ Smartphone-based commissioning
- ✅ On-site troubleshooting without laptop
- ✅ Quick parameter adjustments

### Industrial Automation
- ✅ Legacy PLC integration
- ✅ SCADA system connectivity
- ✅ Remote parameter tuning

## 📦 Download

**GitHub Release:** [v1.4.0](https://github.com/vwetter/esp32-modbus-gateway/releases/tag/v1.4.0)

### Files Included
- `esp32-modbus-gateway.ino` - Main firmware (1,730 lines)
- `README.md` - Full documentation
- `CHANGELOG.md` - Version history
- `ARDUINO_SETUP.md` - Installation guide
- `libraries.txt` - Required libraries

## 🤝 Credits

### Contributors
- [@vwetter](https://github.com/vwetter) - Development and testing

### Special Thanks
- Deye inverter users for compatibility testing
- Mobile UI testers for feedback
- Open source community for excellent libraries

## 💬 Feedback

Questions or suggestions? 
- Open an [issue](https://github.com/vwetter/esp32-modbus-gateway/issues)
- Start a [discussion](https://github.com/vwetter/esp32-modbus-gateway/discussions)

## ⭐ Support the Project

If this release helped you, please:
- ⭐ Star the repository
- 📢 Share with others
- 🐛 Report bugs if you find any
- 💡 Suggest improvements

---

**Thank you for using ESP32 Modbus Gateway!**

*Made with ❤️ by [@vwetter](https://github.com/vwetter)*
