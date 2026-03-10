# Release Notes - v1.4.1

**Release Date:** March 10, 2026

## 📚 Documentation Improvements

### Enhanced Hardware Support Documentation

#### Standard RS485 Module Support
The WIRING.md documentation has been significantly expanded to include support for **standard RS485 modules** (not just MAX485):

- **New section:** "Standard RS485 Module (without DE/RE pins)"
- **Simplified wiring diagram** for standard RS485 modules
- **Clear comparison** between MAX485 and standard RS485 modules
- **Direct code reference** to `MODBUS_DE_PIN` configuration

#### Key Documentation Additions
1. **Standard RS485 wiring guide** with simplified connection diagram
2. **Configuration examples:**
   - `#define MODBUS_DE_PIN -1` for standard RS485 modules
   - `#define MODBUS_DE_PIN 4` for MAX485 modules
3. **Explanation** of automatic direction handling in standard modules
4. **Connection table** showing which pins are used/unused

### What's Different

**Standard RS485 Modules:**
- ✅ Automatic direction switching (no GPIO4 needed)
- ✅ Simpler wiring (3 signal lines: TX, RX, GND)
- ✅ Lower cost
- ✅ Same reliability as MAX485

**MAX485 Modules:**
- Optional explicit direction control via DE/RE pins (GPIO4)
- More advanced control options

## 🔧 Technical Details

Both module types work equally well with the gateway:

```cpp
// For standard RS485 modules WITHOUT DE/RE control
#define MODBUS_DE_PIN     -1

// For MAX485 modules WITH DE/RE control on GPIO4
#define MODBUS_DE_PIN     4
```

The firmware automatically uses the appropriate communication protocol based on this setting.

## 📖 Documentation Files Updated
- `docs/WIRING.md` - Added new section for standard RS485 modules

## ✨ Benefits

- **Wider hardware compatibility** - Users can now use more affordable standard RS485 modules
- **Clearer documentation** - New users understand module differences
- **Fewer support questions** - Direct code-documentation linkage
- **Cost reduction** - Standard modules are often $0.50-$1 cheaper

## 🔄 Backward Compatibility

✅ **100% backward compatible** - No code changes needed for existing deployments. All v1.4.0 installations continue to work without modification.
