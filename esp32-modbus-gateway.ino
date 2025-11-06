/*
  ESP32 Modbus RTU <-> Modbus TCP Gateway
  With Persistent Configuration Storage
  
  Version: 1.4.0
  Date: 2025-11-06
  Author: vwetter
  
  Features v1.4.0:
  - Mobile-friendly button layout (Read/Write side-by-side)
  - Shortened button labels for better mobile UX
  
  Features v1.3.3:
  - Changed Write to use FC 0x10 (Write Multiple Registers) for Deye compatibility
  - Some devices only accept FC 0x10 even for single register writes
  
  Features v1.3.2:
  - CRITICAL FIX: Removed static String variables in async callbacks (caused ESP32 crashes)
  - Added /api/errors/reset endpoint
  - Improved error handling with JSON parse validation
  - Fixed memory corruption issues in Write operations
  - FreeRTOS task for async Modbus writes (non-blocking)
  
  Features v1.3.1:
  - Added console.log debugging for Write operations
  - Improved error handling in modbusWrite()
  
  Features v1.3.0:
  - Modbus Write support with extended timeout for slow devices (Deye inverters)
  - Detailed write logging with timestamps and duration
  - WiFi setup page for initial configuration
  - Serial log buffer for UI display
  - Memory optimizations
*/

#define FIRMWARE_VERSION "1.4.0"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <deque>

// ========================= CONFIGURATION =========================

// WiFi Settings
const char* AP_SSID      = "AP-modbus-gw";
const char* AP_PASS      = "initpass";

// WiFi Credentials (loaded from NVS)
String wifi_ssid = "";
String wifi_password = "";
bool wifi_configured = false;

// OTA Settings
const char* OTA_HOSTNAME = "esp32-modbus-gw";
const char* OTA_PASSWORD = "esphome123";  // ⚠️ Change this for production!

// Hardware Pins
#define MODBUS_UART       Serial2
#define MODBUS_TX_PIN     17
#define MODBUS_RX_PIN     16
#define MODBUS_DE_PIN     -1  // -1 for standard RS485 Module, 4 for MAX485

// UART Default Settings (used on first boot only)
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_STOPBITS 1
#define MODBUS_DEFAULT_DATABITS 8
#define MODBUS_DEFAULT_PARITY   'N'

// Network Ports
#define WEB_PORT        80
#define MODBUS_TCP_PORT 502

// Modbus Timing
#define MODBUS_RTU_RX_TIMEOUT_MS 2000              // Increased from 1000ms
#define MODBUS_RTU_INTER_FRAME_DELAY_MS 50         // Increased from 10ms
#define MODBUS_RTU_POST_TRANSACTION_DELAY_MS 75    // NEW: Delay after transaction
#define MAX_LOG_ENTRIES 100  // Sufficient for typical usage
#define MODBUS_RTU_MAX_FRAME 256

// Modbus Function Codes
#define MODBUS_FC_READ_COILS              0x01
#define MODBUS_FC_READ_DISCRETE_INPUTS    0x02
#define MODBUS_FC_READ_HOLDING_REGISTERS  0x03
#define MODBUS_FC_READ_INPUT_REGISTERS    0x04
#define MODBUS_FC_WRITE_SINGLE_COIL       0x05
#define MODBUS_FC_WRITE_SINGLE_REGISTER   0x06
#define MODBUS_FC_WRITE_MULTIPLE_COILS    0x0F
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// ========================= GLOBALS =========================
AsyncWebServer webServer(WEB_PORT);
AsyncServer* modbusTcpServer = nullptr;

Preferences preferences;

struct UARTConfig {
  uint32_t baud;
  uint8_t stop_bits;
  uint8_t data_bits;
  char parity;
} uartCfg;

std::deque<String> logBuffer;
unsigned long reqCount = 0;
unsigned long errCount = 0;
unsigned long tcpConnections = 0;
unsigned long startMs = 0;

SemaphoreHandle_t rtuMutex = NULL;

// Task structure for async Modbus writes
struct ModbusWriteTask {
  uint8_t slave_id;
  uint16_t address;
  uint16_t value;
};

// ========================= FORWARD DECLARATIONS =========================
void addLog(const String &msg, const char* level = "info");
bool rtu_write_single_register(uint8_t slave_id, uint16_t address, uint16_t value);

// ========================= UTILITIES =========================
static uint16_t crc16_modbus(const uint8_t* buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else
        crc >>= 1;
    }
  }
  return crc;
}

void addLog(const String &msg, const char* level) {
  String entry = "[" + String((millis() - startMs) / 1000) + "s] [" + String(level) + "] " + msg;
  Serial.println(entry);
  logBuffer.push_back(entry);
  if (logBuffer.size() > MAX_LOG_ENTRIES) logBuffer.pop_front();
}

void setDE(bool enable) {
  if (MODBUS_DE_PIN >= 0) {
    digitalWrite(MODBUS_DE_PIN, enable ? HIGH : LOW);
  }
}

String bytesToHex(const uint8_t* data, size_t len) {
  String result = "";
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) result += "0";
    result += String(data[i], HEX);
    if (i < len - 1) result += " ";
  }
  result.toUpperCase();
  return result;
}

// ========================= PERSISTENT STORAGE =========================

void loadConfig() {
  preferences.begin("modbus-gw", false);
  
  uartCfg.baud = preferences.getUInt("baud", MODBUS_DEFAULT_BAUD);
  uartCfg.stop_bits = preferences.getUChar("stop_bits", MODBUS_DEFAULT_STOPBITS);
  uartCfg.data_bits = preferences.getUChar("data_bits", MODBUS_DEFAULT_DATABITS);
  uartCfg.parity = preferences.getChar("parity", MODBUS_DEFAULT_PARITY);
  
  reqCount = preferences.getULong("req_count", 0);
  errCount = preferences.getULong("err_count", 0);
  
  // Load WiFi configuration
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_password", "");
  wifi_configured = (wifi_ssid.length() > 0);
  
  preferences.end();
  
  addLog("Config loaded: " + String(uartCfg.baud) + " " + 
         String(uartCfg.data_bits) + String(uartCfg.parity) + 
         String(uartCfg.stop_bits), "info");
  
  if (wifi_configured) {
    addLog("WiFi config loaded: " + wifi_ssid, "info");
  } else {
    addLog("No WiFi config found - will start in AP mode", "info");
  }
}

void saveUartConfig() {
  preferences.begin("modbus-gw", false);
  
  preferences.putUInt("baud", uartCfg.baud);
  preferences.putUChar("stop_bits", uartCfg.stop_bits);
  preferences.putUChar("data_bits", uartCfg.data_bits);
  preferences.putChar("parity", uartCfg.parity);
  
  preferences.end();
  
  addLog("Config saved to NVS", "success");
}

void saveWifiConfig(const String& ssid, const String& password) {
  preferences.begin("modbus-gw", false);
  
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_password", password);
  
  preferences.end();
  
  wifi_ssid = ssid;
  wifi_password = password;
  wifi_configured = true;
  
  addLog("WiFi config saved: " + ssid, "success");
}

void clearWifiConfig() {
  preferences.begin("modbus-gw", false);
  
  preferences.remove("wifi_ssid");
  preferences.remove("wifi_password");
  
  preferences.end();
  
  wifi_ssid = "";
  wifi_password = "";
  wifi_configured = false;
  
  addLog("WiFi config cleared", "warning");
}

void saveStats() {
  preferences.begin("modbus-gw", false);
  preferences.putULong("req_count", reqCount);
  preferences.putULong("err_count", errCount);
  preferences.end();
}

void factoryReset() {
  preferences.begin("modbus-gw", false);
  preferences.clear();
  preferences.end();
  
  addLog("Factory reset completed", "warning");
  
  uartCfg.baud = MODBUS_DEFAULT_BAUD;
  uartCfg.stop_bits = MODBUS_DEFAULT_STOPBITS;
  uartCfg.data_bits = MODBUS_DEFAULT_DATABITS;
  uartCfg.parity = MODBUS_DEFAULT_PARITY;
  
  // Clear WiFi configuration
  wifi_ssid = "";
  wifi_password = "";
  wifi_configured = false;
}

// ========================= EMBEDDED HTML =========================
const char WIFI_SETUP_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .card {
            background: white;
            border-radius: 12px;
            padding: 32px;
            max-width: 450px;
            width: 100%;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        h1 { color: #2d3748; margin-bottom: 8px; }
        .subtitle { color: #718096; margin-bottom: 24px; font-size: 14px; }
        .form-group { margin-bottom: 20px; }
        label {
            display: block;
            margin-bottom: 8px;
            color: #4a5568;
            font-weight: 500;
            font-size: 14px;
        }
        input {
            width: 100%;
            padding: 12px;
            border: 2px solid #e2e8f0;
            border-radius: 8px;
            font-size: 14px;
            transition: border-color 0.2s;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            background: #667eea;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 8px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            width: 100%;
            transition: background 0.2s;
        }
        button:hover { background: #5a67d8; }
        button:active { background: #4c51bf; }
        .info {
            background: #ebf8ff;
            border-left: 4px solid #4299e1;
            padding: 12px;
            border-radius: 4px;
            margin-bottom: 20px;
            font-size: 13px;
            color: #2c5282;
        }
        .message {
            padding: 12px;
            border-radius: 8px;
            margin-bottom: 20px;
            font-size: 14px;
            display: none;
        }
        .message.success { background: #c6f6d5; color: #22543d; border: 1px solid #9ae6b4; }
        .message.error { background: #fed7d7; color: #742a2a; border: 1px solid #fc8181; }
    </style>
</head>
<body>
    <div class="card">
        <h1>🔧 WiFi Setup</h1>
        <div class="subtitle">ESP32 Modbus Gateway - Initial Configuration</div>
        
        <div class="info">
            📡 Connected to AP: <strong>AP-modbus-gw</strong><br>
            Configure your WiFi credentials below to connect the gateway to your network.
        </div>
        
        <div id="message" class="message"></div>
        
        <form id="wifi-form">
            <div class="form-group">
                <label for="ssid">WiFi Network (SSID)</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Enter WiFi network name">
            </div>
            
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" required placeholder="Enter WiFi password">
            </div>
            
            <button type="submit">💾 Save & Connect</button>
        </form>
    </div>
    
    <script>
        document.getElementById('wifi-form').addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const messageDiv = document.getElementById('message');
            
            try {
                const response = await fetch('/api/wifi/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid: ssid, password: password })
                });
                
                const data = await response.json();
                
                if (data.success) {
                    messageDiv.className = 'message success';
                    messageDiv.style.display = 'block';
                    messageDiv.textContent = '✅ Configuration saved! Device will restart and connect to ' + ssid + '. Please wait 10 seconds and connect to your WiFi network.';
                } else {
                    messageDiv.className = 'message error';
                    messageDiv.style.display = 'block';
                    messageDiv.textContent = '❌ Failed to save configuration. Please try again.';
                }
            } catch (error) {
                messageDiv.className = 'message error';
                messageDiv.style.display = 'block';
                messageDiv.textContent = '❌ Error: ' + error.message;
            }
        });
    </script>
</body>
</html>
)rawliteral";

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Modbus Gateway</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        .card {
            background: white;
            border-radius: 12px;
            padding: 24px;
            margin-bottom: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        h1 { color: #2d3748; margin-bottom: 8px; }
        .subtitle { color: #718096; margin-bottom: 24px; }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 24px;
        }
        .stat {
            background: #f7fafc;
            padding: 16px;
            border-radius: 8px;
            border-left: 4px solid #667eea;
        }
        .stat-label {
            font-size: 12px;
            color: #718096;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .stat-value {
            font-size: 24px;
            font-weight: bold;
            color: #2d3748;
            margin-top: 4px;
        }
        .form-group { margin-bottom: 16px; }
        label {
            display: block;
            margin-bottom: 6px;
            color: #4a5568;
            font-weight: 500;
        }
        input, select {
            width: 100%;
            padding: 10px;
            border: 2px solid #e2e8f0;
            border-radius: 6px;
            font-size: 14px;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            background: #667eea;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: background 0.2s;
        }
        button:hover { background: #5568d3; }
        button:disabled {
            background: #cbd5e0;
            cursor: not-allowed;
        }
        .button-group {
            display: flex;
            gap: 8px;
            flex-wrap: wrap;
        }
        .button-group button {
            flex: 1;
            min-width: 140px;
        }
        .btn-secondary {
            background: #718096;
        }
        .btn-secondary:hover { background: #5a6778; }
        .btn-danger {
            background: #e53e3e;
            margin-left: 8px;
        }
        .btn-danger:hover { background: #c53030; }
        .logs {
            background: #1a202c;
            color: #e2e8f0;
            padding: 16px;
            border-radius: 8px;
            max-height: 400px;
            overflow-y: auto;
            font-family: 'Courier New', monospace;
            font-size: 12px;
            line-height: 1.6;
        }
        .log-entry { margin-bottom: 4px; }
        .log-info { color: #90cdf4; }
        .log-success { color: #68d391; }
        .log-warning { color: #f6ad55; }
        .log-error { color: #fc8181; }
        .log-modbus { color: #d6bcfa; }
        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 12px;
            font-weight: 600;
        }
        .status-online {
            background: #c6f6d5;
            color: #22543d;
        }
        .status-offline {
            background: #fed7d7;
            color: #742a2a;
        }
        .result {
            margin-top: 16px;
            padding: 12px;
            border-radius: 6px;
            background: #f7fafc;
            border-left: 4px solid #667eea;
        }
        .tabs {
            display: flex;
            gap: 8px;
            margin-bottom: 16px;
            border-bottom: 2px solid #e2e8f0;
        }
        .tab {
            padding: 12px 24px;
            cursor: pointer;
            border: none;
            background: none;
            font-weight: 600;
            color: #718096;
            border-bottom: 3px solid transparent;
            transition: all 0.2s;
        }
        .tab.active {
            color: #667eea;
            border-bottom-color: #667eea;
        }
        .tab-content { display: none; }
        .tab-content.active { display: block; }
        .info-box {
            background: #edf2f7;
            border-left: 4px solid #4299e1;
            padding: 12px;
            margin-bottom: 16px;
            border-radius: 4px;
            font-size: 14px;
            color: #2d3748;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="card">
            <h1>🔌 ESP32 Modbus Gateway <span style="font-size: 0.5em; color: #a0aec0;">v<span id="version-header">...</span></span></h1>
            <p class="subtitle" id="subtitle">RTU to TCP Bridge</p>
            
            <div class="grid">
                <div class="stat">
                    <div class="stat-label">WiFi Status</div>
                    <div class="stat-value" id="wifi-status">
                        <span class="status-badge status-offline">Loading...</span>
                    </div>
                </div>
                <div class="stat">
                    <div class="stat-label">Uptime</div>
                    <div class="stat-value" id="uptime">-</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Requests</div>
                    <div class="stat-value" id="requests">0</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Errors</div>
                    <div class="stat-value" id="errors">0</div>
                    <div style="margin-top:6px;"><button onclick="resetErrors()" class="btn-secondary">Reset Errors</button></div>
                </div>
                <div class="stat">
                    <div class="stat-label">Free Heap</div>
                    <div class="stat-value" id="heap">-</div>
                </div>
                <div class="stat">
                    <div class="stat-label">Baud Rate</div>
                    <div class="stat-value" id="baud">9600</div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="tabs">
                <button class="tab active" onclick="switchTab('modbus')">Modbus Test</button>
                <button class="tab" onclick="switchTab('uart')">UART Config</button>
                <button class="tab" onclick="switchTab('logs')">Logs</button>
                <button class="tab" onclick="switchTab('serial')">Serial Log</button>
            </div>

            <div id="modbus-tab" class="tab-content active">
                <h2 style="margin-bottom: 16px;">Modbus Read/Write</h2>
                
                <div class="form-group">
                    <label>Slave ID</label>
                    <input type="number" id="slave-id" value="1" min="0" max="247">
                </div>

                <div class="form-group">
                    <label>Register Address</label>
                    <input type="number" id="address" value="0" min="0" max="65535">
                </div>
                
                <div class="form-group">
                    <label>Quantity (for read)</label>
                    <input type="number" id="quantity" value="1" min="1" max="125">
                </div>
                
                <div class="form-group">
                    <label>Value (for write)</label>
                    <input type="number" id="value" value="0" min="0" max="65535">
                </div>
                
                <div class="button-group">
                    <button onclick="modbusRead()">📖 Read Regs</button>
                    <button onclick="modbusWrite()" class="btn-secondary">✏️ Write Reg</button>
                </div>
                
                <div id="modbus-result"></div>
            </div>

            <div id="uart-tab" class="tab-content">
                <h2 style="margin-bottom: 16px;">UART Configuration</h2>
                
                <div class="info-box">
                    💾 Settings are saved permanently and persist across reboots
                </div>

                <div class="form-group">
                    <label>Baud Rate</label>
                    <select id="uart-baud">
                        <option value="1200">1200</option>
                        <option value="2400">2400</option>
                        <option value="4800">4800</option>
                        <option value="9600" selected>9600</option>
                        <option value="19200">19200</option>
                        <option value="38400">38400</option>
                        <option value="57600">57600</option>
                        <option value="115200">115200</option>
                    </select>
                </div>

                <div class="form-group">
                    <label>Data Bits</label>
                    <select id="uart-databits">
                        <option value="7">7</option>
                        <option value="8" selected>8</option>
                    </select>
                </div>

                <div class="form-group">
                    <label>Parity</label>
                    <select id="uart-parity">
                        <option value="N" selected>None</option>
                        <option value="E">Even</option>
                        <option value="O">Odd</option>
                    </select>
                </div>

                <div class="form-group">
                    <label>Stop Bits</label>
                    <select id="uart-stopbits">
                        <option value="1" selected>1</option>
                        <option value="2">2</option>
                    </select>
                </div>

                <button onclick="updateUartConfig()">💾 Save & Apply Config</button>
                <button onclick="restartDevice()" class="btn-secondary">🔄 Restart Device</button>
                <button onclick="resetWifiConfig()" class="btn-danger">📶 Reset WiFi Config</button>
                <button onclick="factoryReset()" class="btn-danger">⚠️ Factory Reset</button>
            </div>

            <div id="logs-tab" class="tab-content">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
                    <h2>System Logs</h2>
                    <button onclick="clearLogs()" class="btn-secondary">Clear Logs</button>
                </div>
                <div class="logs" id="logs-container">
                    <div class="log-entry">Loading logs...</div>
                </div>
            </div>

            <div id="serial-tab" class="tab-content">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
                    <h2>Serial Log (Live)</h2>
                    <span style="font-size: 0.9em; color: #718096;">Auto-refresh every 2s</span>
                </div>
                <div class="info-box">
                    📝 This shows the real-time serial output including all Modbus RTU transactions, write operations with timestamps and durations.
                </div>
                <div class="logs" id="serial-log-container" style="max-height: 500px; overflow-y: auto; font-family: 'Courier New', monospace; font-size: 12px; background: #1a202c; color: #e2e8f0; padding: 12px; border-radius: 6px;">
                    <div style="color: #a0aec0;">Loading serial log...</div>
                </div>
            </div>
        </div>
    </div>

    <script>
        function switchTab(tab) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            document.querySelector(`button[onclick="switchTab('${tab}')"]`).classList.add('active');
            document.getElementById(`${tab}-tab`).classList.add('active');
            if (tab === 'logs') loadLogs();
            if (tab === 'uart') loadUartConfig();
            if (tab === 'serial') loadSerialLog();
        }

        async function updateStatus() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();
                
                // Update version in header
                if (data.version) {
                    document.getElementById('version-header').textContent = data.version;
                }
                
                document.getElementById('wifi-status').innerHTML = 
                    data.wifi_connected 
                    ? '<span class="status-badge status-online">Connected</span>' 
                    : '<span class="status-badge status-offline">Disconnected</span>';
                
                const h = Math.floor(data.uptime_s / 3600);
                const m = Math.floor((data.uptime_s % 3600) / 60);
                const s = data.uptime_s % 60;
                document.getElementById('uptime').textContent = `${h}h ${m}m ${s}s`;
                
                document.getElementById('requests').textContent = data.request_count;
                document.getElementById('errors').textContent = data.error_count;
                document.getElementById('heap').textContent = Math.floor(data.free_heap / 1024) + ' KB';
                document.getElementById('baud').textContent = data.modbus_baud;
                
                if (data.wifi_connected) {
                    document.getElementById('subtitle').textContent = 
                        `${data.wifi_ssid} - ${data.wifi_ip} (${data.wifi_rssi} dBm)`;
                }
            } catch (e) { console.error('Status error:', e); }
        }

        async function resetErrors() {
            if (!confirm('Reset error counter?')) return;
            try {
                const res = await fetch('/api/errors/reset', { method: 'POST' });
                const data = await res.json();
                if (data.success) {
                    alert('Error counter reset');
                    updateStatus();
                } else {
                    alert('Reset failed');
                }
            } catch (e) { alert('Error: ' + e.message); }
        }

        async function loadUartConfig() {
            try {
                const response = await fetch('/api/uart/config');
                const data = await response.json();
                document.getElementById('uart-baud').value = data.baud_rate;
                document.getElementById('uart-databits').value = data.data_bits;
                document.getElementById('uart-parity').value = data.parity;
                document.getElementById('uart-stopbits').value = data.stop_bits;
            } catch (e) { console.error('Config load error:', e); }
        }

        async function modbusRead() {
            const slaveId = parseInt(document.getElementById('slave-id').value);
            const address = parseInt(document.getElementById('address').value);
            const quantity = parseInt(document.getElementById('quantity').value);
            
            const resultDiv = document.getElementById('modbus-result');
            resultDiv.innerHTML = '<div class="result">⏳ Reading...</div>';
            
            try {
                const response = await fetch('/api/modbus/read', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ slave_id: slaveId, address: address, quantity: quantity })
                });
                
                const data = await response.json();
                
                if (data.success) {
                    let html = '<div class="result"><strong>✅ Read successful!</strong><br><br>';
                    html += `<strong>Slave:</strong> ${data.slave_id}<br>`;
                    html += `<strong>Address:</strong> ${data.address}<br>`;
                    html += `<strong>Values:</strong><br>`;
                    data.values.forEach((val, idx) => {
                        html += `&nbsp;&nbsp;[${data.address + idx}] = ${val} (0x${val.toString(16).toUpperCase().padStart(4,'0')})<br>`;
                    });
                    html += '</div>';
                    resultDiv.innerHTML = html;
                } else {
                    resultDiv.innerHTML = '<div class="result">❌ Read failed! Check logs.</div>';
                }
            } catch (error) {
                resultDiv.innerHTML = '<div class="result">❌ Error: ' + error.message + '</div>';
            }
        }

        async function modbusWrite() {
            const slaveId = parseInt(document.getElementById('slave-id').value);
            const address = parseInt(document.getElementById('address').value);
            const value = parseInt(document.getElementById('value').value);
            
            const resultDiv = document.getElementById('modbus-result');
            resultDiv.innerHTML = '<div class="result">⏳ Writing...</div>';
            
            console.log('Write request:', { slave_id: slaveId, address: address, value: value });
            
            try {
                const response = await fetch('/api/modbus/write', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ slave_id: slaveId, address: address, value: value })
                });
                
                console.log('Write response status:', response.status);
                
                if (!response.ok) {
                    throw new Error('HTTP ' + response.status);
                }
                
                const data = await response.json();
                console.log('Write response data:', data);
                
                if (response.status === 202 || data.status === 'accepted') {
                    resultDiv.innerHTML = `<div class="result">✅ Write command accepted!<br><br>Address: ${address}, Value: ${value} (0x${value.toString(16).toUpperCase().padStart(4,'0')})<br><br>⏳ Check Serial Log tab for result (operation takes 5-8 seconds for slow devices)</div>`;
                } else if (data.success) {
                    resultDiv.innerHTML = `<div class="result">✅ Wrote ${value} (0x${value.toString(16).toUpperCase().padStart(4,'0')}) to address ${address}</div>`;
                } else {
                    resultDiv.innerHTML = '<div class="result">❌ Write failed! Check logs.</div>';
                }
            } catch (error) {
                console.error('Write error:', error);
                resultDiv.innerHTML = '<div class="result">❌ Error: ' + error.message + '</div>';
            }
        }

        async function updateUartConfig() {
            const baud = parseInt(document.getElementById('uart-baud').value);
            const databits = parseInt(document.getElementById('uart-databits').value);
            const parity = document.getElementById('uart-parity').value;
            const stopbits = parseInt(document.getElementById('uart-stopbits').value);
            
            try {
                const response = await fetch('/api/uart/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ 
                        baud_rate: baud, 
                        data_bits: databits, 
                        parity: parity, 
                        stop_bits: stopbits 
                    })
                });
                
                const data = await response.json();
                if (data.success) {
                    alert('✅ UART config saved permanently!\nSettings will persist after reboot.');
                    updateStatus();
                } else {
                    alert('❌ Failed to update config');
                }
            } catch (error) {
                alert('❌ Error: ' + error.message);
            }
        }

        async function loadLogs() {
            try {
                const response = await fetch('/api/logs');
                const data = await response.json();
                
                const logsDiv = document.getElementById('logs-container');
                if (data.logs && data.logs.length > 0) {
                    logsDiv.innerHTML = data.logs.map(log => {
                        let className = 'log-info';
                        if (log.includes('[success]')) className = 'log-success';
                        else if (log.includes('[warning]')) className = 'log-warning';
                        else if (log.includes('[error]')) className = 'log-error';
                        else if (log.includes('[modbus]')) className = 'log-modbus';
                        return `<div class="log-entry ${className}">${log}</div>`;
                    }).join('');
                    logsDiv.scrollTop = logsDiv.scrollHeight;
                } else {
                    logsDiv.innerHTML = '<div class="log-entry">No logs</div>';
                }
            } catch (error) {
                console.error('Log error:', error);
            }
        }

        async function clearLogs() {
            if (!confirm('Clear all logs?')) return;
            try {
                await fetch('/api/logs/clear', { method: 'POST' });
                loadLogs();
            } catch (e) { alert('Failed to clear logs'); }
        }

        async function loadSerialLog() {
            try {
                const response = await fetch('/api/seriallog');
                const data = await response.json();
                
                const container = document.getElementById('serial-log-container');
                if (data.logs && data.logs.length > 0) {
                    container.innerHTML = data.logs.map(log => {
                        // Color coding for different log types
                        let color = '#e2e8f0';
                        if (log.includes('[success]') || log.includes('SUCCESS')) color = '#48bb78';
                        else if (log.includes('[error]') || log.includes('FAILED')) color = '#f56565';
                        else if (log.includes('[warning]')) color = '#ed8936';
                        else if (log.includes('[modbus]') || log.includes('RTU')) color = '#4299e1';
                        
                        return `<div style="color: ${color}; margin-bottom: 4px;">${log}</div>`;
                    }).join('');
                    container.scrollTop = container.scrollHeight;
                } else {
                    container.innerHTML = '<div style="color: #a0aec0;">No serial log entries yet...</div>';
                }
            } catch (error) {
                console.error('Serial log error:', error);
                document.getElementById('serial-log-container').innerHTML = 
                    '<div style="color: #f56565;">❌ Error loading serial log</div>';
            }
        }

        async function restartDevice() {
            if (!confirm('Restart the device?')) return;
            try {
                await fetch('/api/restart', { method: 'POST' });
                alert('Restarting... Wait 10 seconds and refresh.');
            } catch (e) { }
        }

        async function resetWifiConfig() {
            if (!confirm('⚠️ Reset WiFi Configuration?\n\nThis will clear the saved WiFi settings and reboot the device in AP mode for reconfiguration.')) return;
            try {
                await fetch('/api/wifi/reset', { method: 'POST' });
                alert('✅ WiFi configuration reset!\nDevice will reboot in AP mode.\nConnect to "AP-modbus-gw" network to reconfigure.');
                setTimeout(() => window.location.reload(), 3000);
            } catch (e) { }
        }

        async function factoryReset() {
            if (!confirm('⚠️ Factory Reset will erase all saved settings!\n\nAre you sure?')) return;
            if (!confirm('This action cannot be undone. Continue?')) return;
            try {
                await fetch('/api/factory-reset', { method: 'POST' });
                alert('✅ Factory reset complete!\nDevice will restart with default settings.');
                setTimeout(() => window.location.reload(), 3000);
            } catch (e) { }
        }

        updateStatus();
        setInterval(updateStatus, 2000);
        
        // Auto-refresh serial log if that tab is active
        setInterval(() => {
            const serialTab = document.getElementById('serial-tab');
            if (serialTab && serialTab.classList.contains('active')) {
                loadSerialLog();
            }
        }, 2000);
    </script>
</body>
</html>
)rawliteral";

// ========================= MODBUS RTU FUNCTIONS =========================

bool rtu_read_registers(uint8_t slave_id, uint8_t function_code, uint16_t address, uint16_t quantity, uint16_t* result_registers) {
  if (xSemaphoreTake(rtuMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    addLog("Failed to acquire RTU mutex", "error");
    return false;
  }
  
  uint8_t tx_frame[8];
  tx_frame[0] = slave_id;
  tx_frame[1] = function_code;
  tx_frame[2] = address >> 8;
  tx_frame[3] = address & 0xFF;
  tx_frame[4] = quantity >> 8;
  tx_frame[5] = quantity & 0xFF;
  
  uint16_t crc = crc16_modbus(tx_frame, 6);
  tx_frame[6] = crc & 0xFF;
  tx_frame[7] = crc >> 8;
  
  addLog("RTU TX: " + bytesToHex(tx_frame, 8), "modbus");
  
  MODBUS_UART.flush();
  while (MODBUS_UART.available()) MODBUS_UART.read();
  
  setDE(true);
  delay(2);
  MODBUS_UART.write(tx_frame, 8);
  MODBUS_UART.flush();
  delay(2);
  setDE(false);
  
  uint32_t start = millis();
  uint8_t rx_buffer[MODBUS_RTU_MAX_FRAME];
  size_t rx_index = 0;
  
  while (millis() - start < MODBUS_RTU_RX_TIMEOUT_MS) {
    if (MODBUS_UART.available()) {
      rx_buffer[rx_index++] = MODBUS_UART.read();
      start = millis();  // Reset timeout on each byte
      
      if (rx_index >= MODBUS_RTU_MAX_FRAME) break;
      
      if (rx_index >= 5 && rx_buffer[1] == function_code) {
        uint8_t byte_count = rx_buffer[2];
        size_t expected_len = 3 + byte_count + 2;
        
        if (rx_index >= expected_len) {
          delay(5);  // Wait a bit to ensure all bytes received
          break;
        }
      }
    }
    delay(1);  // Small delay to prevent busy-waiting
  }
  
  delay(MODBUS_RTU_POST_TRANSACTION_DELAY_MS);
  
  xSemaphoreGive(rtuMutex);
  
  if (rx_index < 5) {
    addLog("RTU RX timeout or incomplete (got " + String(rx_index) + " bytes)", "error");
    errCount++;
    return false;
  }
  
  addLog("RTU RX: " + bytesToHex(rx_buffer, rx_index), "modbus");
  
  uint16_t expected_crc = crc16_modbus(rx_buffer, rx_index - 2);
  uint16_t received_crc = rx_buffer[rx_index - 2] | (rx_buffer[rx_index - 1] << 8);
  
  if (expected_crc != received_crc) {
    addLog("RTU CRC error - Expected: " + String(expected_crc, HEX) + ", Got: " + String(received_crc, HEX), "error");
    errCount++;
    return false;
  }
  
  if (rx_buffer[0] != slave_id) {
    addLog("RTU slave ID mismatch", "error");
    errCount++;
    return false;
  }
  
  if (rx_buffer[1] & 0x80) {
    addLog("RTU exception: " + String(rx_buffer[2]), "error");
    errCount++;
    return false;
  }
  
  if (rx_buffer[1] != function_code) {
    addLog("RTU function code mismatch", "error");
    errCount++;
    return false;
  }
  
  uint8_t byte_count = rx_buffer[2];
  if (byte_count != quantity * 2) {
    addLog("RTU byte count mismatch", "error");
    errCount++;
    return false;
  }
  
  for (uint16_t i = 0; i < quantity; i++) {
    result_registers[i] = (rx_buffer[3 + i * 2] << 8) | rx_buffer[4 + i * 2];
  }
  
  reqCount++;
  return true;
}

bool rtu_write_single_register(uint8_t slave_id, uint16_t address, uint16_t value) {
  // Try FC 0x10 (Write Multiple Registers) first - some devices like Deye only accept this
  if (xSemaphoreTake(rtuMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    addLog("Failed to acquire RTU mutex for write", "error");
    return false;
  }
  
  unsigned long write_start = millis();
  addLog("RTU Write Single: Start - Addr: " + String(address) + ", Val: " + String(value) + " (using FC 0x10)", "info");
  
  // Build FC 0x10 frame (Write Multiple Registers - even for single register)
  uint8_t tx_frame[13];
  tx_frame[0] = slave_id;
  tx_frame[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;  // 0x10
  tx_frame[2] = address >> 8;
  tx_frame[3] = address & 0xFF;
  tx_frame[4] = 0x00;  // Quantity high byte (1 register)
  tx_frame[5] = 0x01;  // Quantity low byte
  tx_frame[6] = 0x02;  // Byte count (2 bytes for 1 register)
  tx_frame[7] = value >> 8;
  tx_frame[8] = value & 0xFF;
  
  uint16_t crc = crc16_modbus(tx_frame, 9);
  tx_frame[9] = crc & 0xFF;
  tx_frame[10] = crc >> 8;
  
  addLog("RTU TX: " + bytesToHex(tx_frame, 11), "modbus");
  
  MODBUS_UART.flush();
  while (MODBUS_UART.available()) MODBUS_UART.read();
  
  setDE(true);
  delay(2);
  MODBUS_UART.write(tx_frame, 11);
  MODBUS_UART.flush();
  delay(2);
  setDE(false);
  
  uint32_t start = millis();
  uint8_t rx_buffer[8];
  size_t rx_index = 0;
  
  while (millis() - start < MODBUS_RTU_RX_TIMEOUT_MS + 3000) {  // Longer timeout for slow devices
    if (MODBUS_UART.available()) {
      rx_buffer[rx_index++] = MODBUS_UART.read();
      start = millis();
      if (rx_index >= 8) break;
    }
    delay(1);  // Prevent watchdog timeout
  }
  
  delay(MODBUS_RTU_POST_TRANSACTION_DELAY_MS);
  
  xSemaphoreGive(rtuMutex);
  
  unsigned long write_duration = millis() - write_start;
  
  if (rx_index < 8) {
    addLog("RTU Write Single: FAILED - Timeout. Duration: " + String(write_duration) + "ms", "error");
    errCount++;
    return false;
  }
  
  addLog("RTU RX: " + bytesToHex(rx_buffer, rx_index), "modbus");
  
  uint16_t expected_crc = crc16_modbus(rx_buffer, 6);
  uint16_t received_crc = rx_buffer[6] | (rx_buffer[7] << 8);
  
  if (expected_crc != received_crc) {
    addLog("RTU Write Single: FAILED - CRC error. Duration: " + String(write_duration) + "ms", "error");
    errCount++;
    return false;
  }
  
  // For FC 0x10, response is: Slave ID, FC, Start Addr (2), Quantity (2), CRC (2)
  // We just check if it's not an error response
  if (rx_buffer[1] & 0x80) {
    addLog("RTU Write Single: FAILED - Exception response: " + String(rx_buffer[2]), "error");
    errCount++;
    return false;
  }
  
  addLog("RTU Write Single: SUCCESS. Duration: " + String(write_duration) + "ms", "success");
  reqCount++;
  return true;
}

// Task to execute Modbus write in background
void modbusWriteTask(void* parameter) {
  ModbusWriteTask* task = (ModbusWriteTask*)parameter;
  
  rtu_write_single_register(task->slave_id, task->address, task->value);
  
  delete task;
  vTaskDelete(NULL);  // Delete this task
}

// ========================= WEB SERVER INITIALIZATION =========================

void initWeb() {
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (wifi_configured) {
      request->send_P(200, "text/html", HTML_PAGE);
    } else {
      request->send_P(200, "text/html", WIFI_SETUP_PAGE);
    }
  });
  
  webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    
    doc["wifi_connected"] = connected;
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["uptime_s"] = (millis() - startMs) / 1000;
    doc["request_count"] = reqCount;
    doc["error_count"] = errCount;
    doc["tcp_connections"] = tcpConnections;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["modbus_baud"] = uartCfg.baud;
    doc["data_bits"] = uartCfg.data_bits;
    doc["parity"] = String(uartCfg.parity);
    doc["stop_bits"] = uartCfg.stop_bits;
    doc["version"] = FIRMWARE_VERSION;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  webServer.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(8192);
    JsonArray logsArray = doc.createNestedArray("logs");
    
    for (const auto& entry : logBuffer) {
      logsArray.add(entry);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  webServer.on("/api/seriallog", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(8192);
    JsonArray logsArray = doc.createNestedArray("logs");
    
    for (const auto& entry : logBuffer) {
      logsArray.add(entry);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  webServer.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request){
    logBuffer.clear();
    addLog("Logs cleared", "info");
    request->send(200, "application/json", "{\"success\":true}");
  });
  
  webServer.on("/api/errors/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    errCount = 0;
    saveStats();
    addLog("Error counter reset", "info");
    request->send(200, "application/json", "{\"success\":true}");
  });
  
  webServer.on("/api/modbus/read", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len != total) return; // Wait for complete body
      
      String body;
      body.reserve(total);
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      
      DynamicJsonDocument reqDoc(256);
      DeserializationError error = deserializeJson(reqDoc, body);
      
      if (error) {
        addLog("Read API: JSON parse error", "error");
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      uint8_t slave_id = reqDoc["slave_id"];
      uint16_t address = reqDoc["address"];
      uint16_t quantity = reqDoc["quantity"];
       
      addLog("API Read: Slave=" + String(slave_id) + " Addr=" + String(address) + " Qty=" + String(quantity), "info");
      
      uint16_t registers[125];
      bool success = rtu_read_registers(slave_id, MODBUS_FC_READ_HOLDING_REGISTERS, address, quantity, registers);
      
      DynamicJsonDocument resDoc(2048);
      resDoc["success"] = success;
      
      if (success) {
        resDoc["slave_id"] = slave_id;
        resDoc["address"] = address;
        JsonArray valuesArray = resDoc.createNestedArray("values");
        for (uint16_t i = 0; i < quantity; i++) {
          valuesArray.add(registers[i]);
        }
      }
      
      String response;
      serializeJson(resDoc, response);
      request->send(200, "application/json", response);
    }
  );
  
  webServer.on("/api/modbus/write", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len != total) return; // Wait for complete body
      
      String body;
      body.reserve(total);
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      
      DynamicJsonDocument reqDoc(256);
      DeserializationError error = deserializeJson(reqDoc, body);
      
      if (error) {
        addLog("Write API: JSON parse error", "error");
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      uint8_t slave_id = reqDoc["slave_id"];
      uint16_t address = reqDoc["address"];
      uint16_t value = reqDoc["value"];
      
      addLog("API Write: Slave=" + String(slave_id) + " Addr=" + String(address) + " Val=" + String(value), "info");
      
      // Create task data
      ModbusWriteTask* task = new ModbusWriteTask();
      task->slave_id = slave_id;
      task->address = address;
      task->value = value;
      
      // Send response immediately
      request->send(202, "application/json", "{\"success\":true,\"status\":\"accepted\"}");
      
      // Execute write in separate FreeRTOS task (non-blocking)
      xTaskCreate(
        modbusWriteTask,      // Task function
        "ModbusWrite",        // Task name
        4096,                 // Stack size
        task,                 // Parameters
        1,                    // Priority
        NULL                  // Task handle
      );
    }
  );
  
  webServer.on("/api/uart/config", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len != total) return; // Wait for complete body
      
      String body;
      body.reserve(total);
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      
      DynamicJsonDocument reqDoc(256);
      DeserializationError error = deserializeJson(reqDoc, body);
      
      if (error) {
        addLog("UART Config API: JSON parse error", "error");
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      uartCfg.baud = reqDoc["baud_rate"];
      uartCfg.data_bits = reqDoc["data_bits"];
      uartCfg.stop_bits = reqDoc["stop_bits"];
      const char* parity_str = reqDoc["parity"];
      uartCfg.parity = parity_str[0];
      
      saveUartConfig();
      
      // Reinitialize UART
      MODBUS_UART.end();
      uint32_t config = SERIAL_8N1;
      if (uartCfg.data_bits == 7) config = (uartCfg.parity == 'E') ? SERIAL_7E1 : (uartCfg.parity == 'O') ? SERIAL_7O1 : SERIAL_7N1;
      else config = (uartCfg.parity == 'E') ? SERIAL_8E1 : (uartCfg.parity == 'O') ? SERIAL_8O1 : SERIAL_8N1;
      
      MODBUS_UART.begin(uartCfg.baud, config, MODBUS_RX_PIN, MODBUS_TX_PIN);
      
      DynamicJsonDocument resDoc(128);
      resDoc["success"] = true;
      
      String response;
      serializeJson(resDoc, response);
      request->send(200, "application/json", response);
    }
  );
  
  webServer.on("/api/wifi/config", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (index + len != total) return; // Wait for complete body
      
      String body;
      body.reserve(total);
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      
      DynamicJsonDocument reqDoc(256);
      DeserializationError error = deserializeJson(reqDoc, body);
      
      if (error) {
        addLog("WiFi Config API: JSON parse error", "error");
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      String ssid = reqDoc["ssid"].as<String>();
      String password = reqDoc["password"].as<String>();
      
      saveWifiConfig(ssid, password);
      
      DynamicJsonDocument resDoc(128);
      resDoc["success"] = true;
      
      String response;
      serializeJson(resDoc, response);
      request->send(200, "application/json", response);
      
      delay(500);
      ESP.restart();
    }
  );
  
  webServer.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    clearWifiConfig();
    request->send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  });
  
  webServer.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  });
  
  webServer.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request){
    factoryReset();
    request->send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  });
  
  webServer.begin();
  addLog("Web server started", "success");
}

// ========================= MODBUS TCP SERVER =========================

void handleModbusTcpClient(void* arg, AsyncClient* client) {
  if (!client) return;
  
  tcpConnections++;
  addLog("Modbus TCP client connected: " + client->remoteIP().toString(), "info");
  
  client->onData([](void* arg, AsyncClient* c, void* data, size_t len){
    if (len < 8) {
      c->close();
      return;
    }
    
    uint8_t* frame = (uint8_t*)data;
    
    // MBAP Header: Transaction ID (2), Protocol ID (2), Length (2), Unit ID (1)
    uint16_t transaction_id = (frame[0] << 8) | frame[1];
    uint16_t length = (frame[4] << 8) | frame[5];
    uint8_t unit_id = frame[6];
    uint8_t function_code = frame[7];
    
    addLog("TCP RX: FC=" + String(function_code, HEX), "modbus");
    
    if (function_code == MODBUS_FC_READ_HOLDING_REGISTERS || function_code == MODBUS_FC_READ_INPUT_REGISTERS) {
      uint16_t address = (frame[8] << 8) | frame[9];
      uint16_t quantity = (frame[10] << 8) | frame[11];
      
      if (quantity > 125) quantity = 125;
      
      uint16_t* registers = (uint16_t*)malloc(quantity * sizeof(uint16_t));
      if (!registers) {
        c->close();
        return;
      }
      
      bool success = rtu_read_registers(unit_id, function_code, address, quantity, registers);
      
      if (success) {
        size_t resp_size = 9 + quantity * 2;
        uint8_t* response = (uint8_t*)malloc(resp_size);
        if (response) {
          response[0] = frame[0];
          response[1] = frame[1];
          response[2] = 0;
          response[3] = 0;
          uint16_t resp_len = 3 + quantity * 2;
          response[4] = resp_len >> 8;
          response[5] = resp_len & 0xFF;
          response[6] = unit_id;
          response[7] = function_code;
          response[8] = quantity * 2;
          
          for (uint16_t i = 0; i < quantity; i++) {
            response[9 + i * 2] = registers[i] >> 8;
            response[10 + i * 2] = registers[i] & 0xFF;
          }
          
          c->write((const char*)response, resp_size);
          free(response);
          addLog("TCP TX: Success", "modbus");
        }
      } else {
        uint8_t error_response[9];
        error_response[0] = frame[0];
        error_response[1] = frame[1];
        error_response[2] = 0;
        error_response[3] = 0;
        error_response[4] = 0;
        error_response[5] = 3;
        error_response[6] = unit_id;
        error_response[7] = function_code | 0x80;
        error_response[8] = 0x04;
        
        c->write((const char*)error_response, 9);
        addLog("TCP TX: Error", "error");
      }
      
      free(registers);
    }
    else if (function_code == MODBUS_FC_WRITE_SINGLE_REGISTER) {
      uint16_t address = (frame[8] << 8) | frame[9];
      uint16_t value = (frame[10] << 8) | frame[11];
      
      bool success = rtu_write_single_register(unit_id, address, value);
      
      if (success) {
        c->write((const char*)frame, 12);
        addLog("TCP TX: Write OK", "modbus");
      } else {
        uint8_t error_response[9];
        error_response[0] = frame[0];
        error_response[1] = frame[1];
        error_response[2] = 0;
        error_response[3] = 0;
        error_response[4] = 0;
        error_response[5] = 3;
        error_response[6] = unit_id;
        error_response[7] = function_code | 0x80;
        error_response[8] = 0x04;
        
        c->write((const char*)error_response, 9);
        addLog("TCP TX: Write Err", "error");
      }
    }
  }, nullptr);
  
  client->onDisconnect([](void* arg, AsyncClient* c){
    addLog("Modbus TCP client disconnected", "info");
  }, nullptr);
}

void initModbusTcp() {
  modbusTcpServer = new AsyncServer(MODBUS_TCP_PORT);
  
  modbusTcpServer->onClient(&handleModbusTcpClient, modbusTcpServer);
  modbusTcpServer->begin();
  
  addLog("Modbus TCP server started on port " + String(MODBUS_TCP_PORT), "success");
}

// ========================= SETUP =========================

void setup() {
  Serial.begin(115200);
  startMs = millis();
  
  addLog("ESP32 Modbus Gateway v" + String(FIRMWARE_VERSION), "info");
  addLog("Booting...", "info");
  
  // Initialize DE pin if used
  if (MODBUS_DE_PIN >= 0) {
    pinMode(MODBUS_DE_PIN, OUTPUT);
    setDE(false);
  }
  
  // Create mutex
  rtuMutex = xSemaphoreCreateMutex();
  if (rtuMutex == NULL) {
    addLog("Failed to create RTU mutex!", "error");
  }
  
  // Load configuration
  loadConfig();
  
  // Initialize UART
  uint32_t uart_config = SERIAL_8N1;
  if (uartCfg.data_bits == 7) {
    uart_config = (uartCfg.parity == 'E') ? SERIAL_7E1 : (uartCfg.parity == 'O') ? SERIAL_7O1 : SERIAL_7N1;
  } else {
    uart_config = (uartCfg.parity == 'E') ? SERIAL_8E1 : (uartCfg.parity == 'O') ? SERIAL_8O1 : SERIAL_8N1;
  }
  
  MODBUS_UART.begin(uartCfg.baud, uart_config, MODBUS_RX_PIN, MODBUS_TX_PIN);
  addLog("UART initialized: " + String(uartCfg.baud) + " baud", "success");
  
  delay(100);
  yield();
  
  // WiFi setup
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.setHostname(OTA_HOSTNAME);
  delay(100);
  
  if (wifi_configured) {
    addLog("Connecting to WiFi: " + wifi_ssid, "info");
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      yield();
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      addLog("WiFi connected! IP: " + WiFi.localIP().toString(), "success");
    } else {
      addLog("WiFi connection failed - Starting AP mode", "warning");
      WiFi.mode(WIFI_AP);
      delay(500);
      yield();
      WiFi.softAP(AP_SSID, AP_PASS);
      delay(1000);
      yield();
      addLog("AP Mode started", "info");
    }
  } else {
    addLog("No WiFi config - Starting AP mode", "info");
    WiFi.mode(WIFI_AP);
    delay(500);
    yield();
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(1000);
    yield();
    addLog("AP Mode started", "info");
  }
  
  delay(500);
  yield();
  
  // Initialize OTA
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    addLog("OTA Update Start: " + type, "info");
  });
  
  ArduinoOTA.onEnd([]() {
    addLog("OTA Update Complete", "success");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    addLog("OTA Error: " + String(error), "error");
  });
  
  ArduinoOTA.begin();
  addLog("OTA initialized", "success");
  
  // Initialize web server
  initWeb();
  
  // Initialize Modbus TCP server
  initModbusTcp();
  
  addLog("Setup complete - Ready!", "success");
}

// ========================= LOOP =========================

void loop() {
  ArduinoOTA.handle();
  
  // Save stats periodically
  static unsigned long lastStatsSave = 0;
  if (millis() - lastStatsSave > 300000) {  // Every 5 minutes
    saveStats();
    lastStatsSave = millis();
  }
  
  delay(10);
}