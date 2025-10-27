/*
  ESP32 Modbus RTU <-> Modbus TCP Gateway
  With Persistent Configuration Storage
  
  Version: 1.1.0
  Date: 2025-01-22
  Author: vwetter
*/

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
#define MODBUS_DE_PIN     4  // -1 for standard RS485 Module, not MAX485

// UART Default Settings (used on first boot only)
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_STOPBITS 1
#define MODBUS_DEFAULT_DATABITS 8
#define MODBUS_DEFAULT_PARITY   'N'

// Network Ports
#define WEB_PORT        80
#define MODBUS_TCP_PORT 502

// Modbus Timing
#define MODBUS_RTU_RX_TIMEOUT_MS 1000
#define MODBUS_RTU_INTER_FRAME_DELAY_MS 10
#define MAX_LOG_ENTRIES 500
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

// ========================= FORWARD DECLARATIONS =========================
void addLog(const String &msg, const char* level = "info");

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
        .btn-secondary {
            background: #718096;
            margin-left: 8px;
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
            <h1>🔌 ESP32 Modbus Gateway</h1>
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
            </div>

            <div id="modbus-tab" class="tab-content active">
                <h2 style="margin-bottom: 16px;">Modbus Read/Write</h2>
                
                <div class="form-group">
                    <label>Slave ID</label>
                    <input type="number" id="slave-id" value="1" min="1" max="247">
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
                
                <button onclick="modbusRead()">📖 Read Registers</button>
                <button onclick="modbusWrite()" class="btn-secondary">✏️ Write Register</button>
                
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
        }

        async function updateStatus() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();
                
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
            
            try {
                const response = await fetch('/api/modbus/write', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ slave_id: slaveId, address: address, value: value })
                });
                
                const data = await response.json();
                
                if (data.success) {
                    resultDiv.innerHTML = `<div class="result">✅ Wrote ${value} (0x${value.toString(16).toUpperCase().padStart(4,'0')}) to address ${address}</div>`;
                } else {
                    resultDiv.innerHTML = '<div class="result">❌ Write failed! Check logs.</div>';
                }
            } catch (error) {
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
    </script>
</body>
</html>
)rawliteral";

// ========================= SETUP HTML (for WiFi configuration) =========================
const char SETUP_HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Modbus Gateway - WiFi Setup</title>
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
        .setup-container {
            background: white;
            border-radius: 12px;
            padding: 32px;
            max-width: 500px;
            width: 100%;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        h1 {
            color: #2d3748;
            margin-bottom: 8px;
            text-align: center;
        }
        .subtitle {
            color: #718096;
            margin-bottom: 24px;
            text-align: center;
        }
        .info-box {
            background: #e6fffa;
            border-left: 4px solid #38b2ac;
            padding: 16px;
            margin-bottom: 24px;
            border-radius: 4px;
            font-size: 14px;
            color: #2d3748;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #4a5568;
            font-weight: 600;
        }
        input {
            width: 100%;
            padding: 12px;
            border: 2px solid #e2e8f0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.2s;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        button {
            width: 100%;
            background: #667eea;
            color: white;
            border: none;
            padding: 14px 24px;
            border-radius: 8px;
            cursor: pointer;
            font-size: 16px;
            font-weight: 600;
            transition: background 0.2s;
            margin-bottom: 12px;
        }
        button:hover {
            background: #5568d3;
        }
        button:disabled {
            background: #cbd5e0;
            cursor: not-allowed;
        }
        .btn-secondary {
            background: #718096;
        }
        .btn-secondary:hover {
            background: #5a6778;
        }
        .status {
            margin-top: 16px;
            padding: 12px;
            border-radius: 6px;
            text-align: center;
            font-weight: 600;
        }
        .status.success {
            background: #c6f6d5;
            color: #22543d;
        }
        .status.error {
            background: #fed7d7;
            color: #742a2a;
        }
        .status.info {
            background: #bee3f8;
            color: #2c5282;
        }
        .wifi-list {
            background: #f7fafc;
            border-radius: 8px;
            max-height: 200px;
            overflow-y: auto;
            margin-bottom: 16px;
        }
        .wifi-item {
            padding: 12px 16px;
            border-bottom: 1px solid #e2e8f0;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .wifi-item:hover {
            background: #edf2f7;
        }
        .wifi-item:last-child {
            border-bottom: none;
        }
        .wifi-name {
            font-weight: 600;
            color: #2d3748;
        }
        .wifi-strength {
            font-size: 12px;
            color: #718096;
        }
        .loading {
            text-align: center;
            color: #718096;
            padding: 20px;
        }
    </style>
</head>
<body>
    <div class="setup-container">
        <h1>🔌 ESP32 Modbus Gateway</h1>
        <p class="subtitle">WiFi Configuration</p>
        
        <div class="info-box">
            <strong>Welcome!</strong><br>
            Configure your WiFi credentials to connect this gateway to your network. 
            After saving, the device will reboot and connect to your WiFi.
        </div>

        <div class="form-group">
            <label>Available WiFi Networks:</label>
            <div id="wifi-list" class="wifi-list">
                <div class="loading">Scanning for networks...</div>
            </div>
            <button onclick="scanWifi()" class="btn-secondary">🔄 Scan Again</button>
        </div>

        <form id="wifi-form">
            <div class="form-group">
                <label for="ssid">WiFi Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Enter your WiFi network name">
            </div>
            
            <div class="form-group">
                <label for="password">WiFi Password:</label>
                <input type="password" id="password" name="password" required placeholder="Enter your WiFi password">
            </div>
            
            <button type="submit" id="submit-btn">💾 Save & Connect</button>
        </form>
        
        <div id="status"></div>
    </div>

    <script>
        async function scanWifi() {
            const wifiList = document.getElementById('wifi-list');
            wifiList.innerHTML = '<div class="loading">Scanning...</div>';
            
            try {
                const response = await fetch('/api/wifi/scan');
                const data = await response.json();
                
                if (data.networks && data.networks.length > 0) {
                    wifiList.innerHTML = data.networks.map(network => `
                        <div class="wifi-item" onclick="selectWifi('${network.ssid}')">
                            <span class="wifi-name">${network.ssid}</span>
                            <span class="wifi-strength">${network.rssi} dBm ${network.encryption ? '🔒' : '🔓'}</span>
                        </div>
                    `).join('');
                } else {
                    wifiList.innerHTML = '<div class="loading">No networks found</div>';
                }
            } catch (error) {
                wifiList.innerHTML = '<div class="loading">Scan failed</div>';
            }
        }

        function selectWifi(ssid) {
            document.getElementById('ssid').value = ssid;
        }

        document.getElementById('wifi-form').addEventListener('submit', async function(e) {
            e.preventDefault();
            
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const statusDiv = document.getElementById('status');
            const submitBtn = document.getElementById('submit-btn');
            
            if (!ssid || !password) {
                statusDiv.innerHTML = '<div class="status error">Please enter both SSID and password</div>';
                return;
            }
            
            submitBtn.disabled = true;
            submitBtn.textContent = '⏳ Saving...';
            statusDiv.innerHTML = '<div class="status info">Saving WiFi configuration...</div>';
            
            try {
                const response = await fetch('/api/wifi/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid: ssid, password: password })
                });
                
                const data = await response.json();
                
                if (data.success) {
                    statusDiv.innerHTML = '<div class="status success">✅ WiFi configuration saved!<br>Device will reboot in 3 seconds...</div>';
                    setTimeout(() => {
                        statusDiv.innerHTML = '<div class="status info">🔄 Rebooting... Please wait 30 seconds then connect to your WiFi network and visit the gateway IP.</div>';
                    }, 3000);
                } else {
                    statusDiv.innerHTML = '<div class="status error">❌ Failed to save configuration</div>';
                    submitBtn.disabled = false;
                    submitBtn.textContent = '💾 Save & Connect';
                }
            } catch (error) {
                statusDiv.innerHTML = '<div class="status error">❌ Error: ' + error.message + '</div>';
                submitBtn.disabled = false;
                submitBtn.textContent = '💾 Save & Connect';
            }
        });

        // Auto-scan on page load
        scanWifi();
    </script>
</body>
</html>
)rawliteral";

// ========================= RTU TRANSACTIONS =========================
bool rtu_transaction(const uint8_t* req, size_t req_len, uint8_t* resp, size_t &resp_len, uint32_t timeout_ms = MODBUS_RTU_RX_TIMEOUT_MS) {
  if (rtuMutex != NULL) {
    if (xSemaphoreTake(rtuMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
      addLog("RTU mutex timeout", "error");
      return false;
    }
  }
  
  if (req_len + 2 > MODBUS_RTU_MAX_FRAME) {
    if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
    return false;
  }
  
  uint8_t tx_frame[MODBUS_RTU_MAX_FRAME];
  memcpy(tx_frame, req, req_len);
  uint16_t crc = crc16_modbus(tx_frame, req_len);
  tx_frame[req_len] = crc & 0xFF;
  tx_frame[req_len + 1] = (crc >> 8) & 0xFF;
  size_t tx_total = req_len + 2;

  addLog("TX: " + bytesToHex(tx_frame, tx_total), "modbus");

  delay(MODBUS_RTU_INTER_FRAME_DELAY_MS);
  while (MODBUS_UART.available()) MODBUS_UART.read();

  setDE(true);
  delayMicroseconds(100);

  size_t written = MODBUS_UART.write(tx_frame, tx_total);
  MODBUS_UART.flush();

  if (written != tx_total) {
    addLog("UART write error: " + String(written) + "/" + String(tx_total), "error");
    setDE(false);
    if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
    return false;
  }

  delayMicroseconds(100);
  setDE(false);

  unsigned long start = millis();
  resp_len = 0;
  unsigned long lastByteTime = 0;
  
  while (millis() - start < timeout_ms) {
    if (MODBUS_UART.available()) {
      int b = MODBUS_UART.read();
      if (b >= 0 && resp_len < MODBUS_RTU_MAX_FRAME) {
        resp[resp_len++] = (uint8_t)b;
        lastByteTime = millis();
      }
    }
    
    if (resp_len >= 5) {
      if (resp[1] & 0x80) {
        if (resp_len >= 5) {
          uint16_t rcrc = crc16_modbus(resp, resp_len - 2);
          uint16_t rframecrc = ((uint16_t)resp[resp_len - 1] << 8) | resp[resp_len - 2];
          if (rcrc == rframecrc) {
            addLog("RX (Exception): " + bytesToHex(resp, resp_len), "modbus");
            if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
            return true;
          }
        }
      }
      
      uint8_t funcCode = resp[1];
      size_t expectedLen = 0;
      
      switch (funcCode) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISCRETE_INPUTS:
        case MODBUS_FC_READ_HOLDING_REGISTERS:
        case MODBUS_FC_READ_INPUT_REGISTERS:
          if (resp_len >= 3) {
            expectedLen = 3 + resp[2] + 2;
          }
          break;
        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
          expectedLen = 8;
          break;
        default:
          if (millis() - lastByteTime > 50) {
            expectedLen = resp_len;
          }
          break;
      }
      
      if (expectedLen > 0 && resp_len >= expectedLen) {
        uint16_t rcrc = crc16_modbus(resp, resp_len - 2);
        uint16_t rframecrc = ((uint16_t)resp[resp_len - 1] << 8) | resp[resp_len - 2];
        if (rcrc == rframecrc) {
          addLog("RX: " + bytesToHex(resp, resp_len), "modbus");
          if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
          return true;
        } else if (resp_len < MODBUS_RTU_MAX_FRAME) {
          continue;
        } else {
          addLog("CRC error", "error");
          if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
          return false;
        }
      }
    }
    
    delay(1);
  }
  
  if (resp_len > 0) {
    addLog("Timeout (" + String(resp_len) + " bytes)", "error");
  } else {
    addLog("No response", "error");
  }
  
  if (rtuMutex != NULL) xSemaphoreGive(rtuMutex);
  return false;
}

bool rtu_read_holding(uint8_t slave, uint16_t addr, uint16_t qty, uint16_t* out_values, uint16_t &out_qty) {
  if (qty > 125) return false;
  
  uint8_t req[6];
  req[0] = slave;
  req[1] = MODBUS_FC_READ_HOLDING_REGISTERS;
  req[2] = highByte(addr);
  req[3] = lowByte(addr);
  req[4] = highByte(qty);
  req[5] = lowByte(qty);

  uint8_t resp[MODBUS_RTU_MAX_FRAME];
  size_t resp_len = 0;
  if (!rtu_transaction(req, 6, resp, resp_len)) return false;

  if (resp_len < 5 || resp[0] != slave) return false;
  
  if (resp[1] & 0x80) {
    addLog("Modbus exception: 0x" + String(resp[2], HEX), "error");
    return false;
  }
  
  if (resp[1] != MODBUS_FC_READ_HOLDING_REGISTERS) return false;
  
  uint8_t byteCount = resp[2];
  out_qty = byteCount / 2;
  for (uint16_t i = 0; i < out_qty; ++i) {
    out_values[i] = ((uint16_t)resp[3 + i*2] << 8) | resp[3 + i*2 + 1];
  }
  return true;
}

bool rtu_write_single(uint8_t slave, uint16_t addr, uint16_t value) {
  uint8_t req[6];
  req[0] = slave;
  req[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
  req[2] = highByte(addr);
  req[3] = lowByte(addr);
  req[4] = highByte(value);
  req[5] = lowByte(value);

  uint8_t resp[MODBUS_RTU_MAX_FRAME];
  size_t resp_len = 0;
  if (!rtu_transaction(req, 6, resp, resp_len)) return false;
  
  if (resp_len < 8 || resp[0] != slave) return false;
  
  if (resp[1] & 0x80) {
    addLog("Modbus exception: 0x" + String(resp[2], HEX), "error");
    return false;
  }
  
  return (resp[1] == MODBUS_FC_WRITE_SINGLE_REGISTER && 
          resp[2] == req[2] && resp[3] == req[3] &&
          resp[4] == req[4] && resp[5] == req[5]);
}

bool rtu_write_multiple(uint8_t slave, uint16_t addr, uint16_t qty, const uint16_t* values) {
  if (qty > 123) return false;
  
  uint8_t req[256];
  uint8_t byteCount = qty * 2;
  
  req[0] = slave;
  req[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
  req[2] = highByte(addr);
  req[3] = lowByte(addr);
  req[4] = highByte(qty);
  req[5] = lowByte(qty);
  req[6] = byteCount;
  
  for (uint16_t i = 0; i < qty; i++) {
    req[7 + i*2] = highByte(values[i]);
    req[8 + i*2] = lowByte(values[i]);
  }
  
  size_t req_len = 7 + byteCount;
  
  uint8_t resp[MODBUS_RTU_MAX_FRAME];
  size_t resp_len = 0;
  if (!rtu_transaction(req, req_len, resp, resp_len)) return false;
  
  if (resp_len < 8 || resp[0] != slave) return false;
  
  if (resp[1] & 0x80) {
    addLog("Modbus exception: 0x" + String(resp[2], HEX), "error");
    return false;
  }
  
  return (resp[1] == MODBUS_FC_WRITE_MULTIPLE_REGISTERS &&
          resp[2] == req[2] && resp[3] == req[3]);
}

// ========================= MODBUS TCP BRIDGE =========================
void onModbusTcpClient(void* arg, AsyncClient* client) {
  tcpConnections++;
  addLog("TCP client: " + client->remoteIP().toString(), "info");

  client->onData([client](void* c, AsyncClient* cl, void* buf, size_t len) {
    (void)c;
    uint8_t* data = (uint8_t*)buf;
    size_t offset = 0;
    
    while (offset < len) {
      size_t remaining = len - offset;
      if (remaining < 7) break;

      uint16_t trans_id = ((uint16_t)data[offset] << 8) | data[offset + 1];
      uint16_t proto_id = ((uint16_t)data[offset + 2] << 8) | data[offset + 3];
      uint16_t len_field = ((uint16_t)data[offset + 4] << 8) | data[offset + 5];
      uint8_t unit_id = data[offset + 6];

      if (proto_id != 0 || len_field == 0 || len_field > 250) break;

      size_t pdu_len = len_field - 1;
      size_t frame_total = 7 + pdu_len;
      if (remaining < frame_total) break;

      uint8_t rtu_req[MODBUS_RTU_MAX_FRAME];
      rtu_req[0] = unit_id;
      if (pdu_len > 0) memcpy(&rtu_req[1], &data[offset + 7], pdu_len);
      size_t rtu_req_len = 1 + pdu_len;

      uint8_t rtu_resp[MODBUS_RTU_MAX_FRAME];
      size_t rtu_resp_len = 0;
      bool ok = rtu_transaction(rtu_req, rtu_req_len, rtu_resp, rtu_resp_len);
      
      if (!ok) {
        errCount++;
        uint8_t mbap[9];
        mbap[0] = data[offset]; mbap[1] = data[offset + 1];
        mbap[2] = 0; mbap[3] = 0;
        mbap[4] = 0; mbap[5] = 3;
        mbap[6] = unit_id;
        mbap[7] = rtu_req[1] | 0x80;
        mbap[8] = 0x0B;
        if (cl->space() >= 9) cl->write((const char*)mbap, 9);
        offset += frame_total;
        continue;
      }

      if (rtu_resp_len < 3) {
        errCount++;
        offset += frame_total;
        continue;
      }
      
      size_t resp_pdu_len = rtu_resp_len - 3;
      uint16_t mbap_len = 1 + resp_pdu_len;
      size_t mbap_total = 7 + resp_pdu_len;
      
      uint8_t *mbap = (uint8_t*)malloc(mbap_total);
      if (!mbap) {
        errCount++;
        offset += frame_total;
        continue;
      }
      
      mbap[0] = data[offset]; mbap[1] = data[offset + 1];
      mbap[2] = 0; mbap[3] = 0;
      mbap[4] = (uint8_t)(mbap_len >> 8);
      mbap[5] = (uint8_t)(mbap_len & 0xFF);
      mbap[6] = unit_id;
      memcpy(&mbap[7], &rtu_resp[1], resp_pdu_len);
      
      if (cl->space() >= mbap_total) {
        cl->write((const char*)mbap, mbap_total);
        reqCount++;
      } else {
        errCount++;
      }
      
      free(mbap);
      offset += frame_total;
    }
  }, nullptr);

  client->onDisconnect([](void* c, AsyncClient* cl) {
    (void)c;
    addLog("TCP disconnected", "info");
    delete cl;
  }, nullptr);
  
  client->onError([](void* c, AsyncClient* cl, int8_t error) {
    (void)c;
    addLog("TCP error: " + String(error), "error");
  }, nullptr);
}

// ========================= WEB API =========================
String getStatusJson() {
  StaticJsonDocument<1024> doc;
  doc["uptime_s"] = (millis() - startMs) / 1000;
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_configured"] = wifi_configured;
  doc["wifi_ssid"] = wifi_configured ? wifi_ssid : WiFi.SSID();
  doc["wifi_ip"] = WiFi.localIP().toString();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["request_count"] = reqCount;
  doc["error_count"] = errCount;
  doc["tcp_connections"] = tcpConnections;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["modbus_baud"] = uartCfg.baud;
  String s;
  serializeJson(doc, s);
  return s;
}

void handleUartConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len)) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }
  
  uint32_t baud = doc["baud_rate"] | uartCfg.baud;
  if (baud < 300 || baud > 115200) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }

  uartCfg.baud = baud;
  uartCfg.stop_bits = doc["stop_bits"] | uartCfg.stop_bits;
  uartCfg.data_bits = doc["data_bits"] | uartCfg.data_bits;
  const char* p = doc["parity"] | "N";
  uartCfg.parity = p[0];
  
  saveUartConfig();
  
  MODBUS_UART.end();
  uint32_t cfg = SERIAL_8N1;
  if (uartCfg.data_bits == 8) {
    if (uartCfg.parity == 'N') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8N1 : SERIAL_8N2;
    else if (uartCfg.parity == 'E') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8E1 : SERIAL_8E2;
    else if (uartCfg.parity == 'O') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8O1 : SERIAL_8O2;
  }
  MODBUS_UART.begin(uartCfg.baud, cfg, MODBUS_RX_PIN, MODBUS_TX_PIN);
  addLog("UART: " + String(uartCfg.baud) + " saved to NVS", "success");
  request->send(200, "application/json", "{\"success\":true}");
}

void handleModbusReadPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len)) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }
  
  uint8_t slave = doc["slave_id"] | 1;
  uint16_t address = doc["address"] | 0;
  uint16_t qty = doc["quantity"] | 1;
  
  if (qty > 125) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }
  
  uint16_t values[125];
  uint16_t got = 0;
  bool ok = rtu_read_holding(slave, address, qty, values, got);
  
  DynamicJsonDocument resp(2048);
  resp["success"] = ok;
  resp["slave_id"] = slave;
  resp["address"] = address;
  
  if (ok) {
    JsonArray arr = resp.createNestedArray("values");
    for (uint16_t i = 0; i < got; ++i) arr.add(values[i]);
  }
  
  String out;
  serializeJson(resp, out);
  request->send(ok ? 200 : 500, "application/json", out);
}

void handleModbusWritePost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len)) {
    request->send(400, "application/json", "{\"success\":false}");
    return;
  }
  
  uint8_t slave = doc["slave_id"] | 1;
  uint16_t address = doc["address"] | 0;
  bool ok = false;
  
  if (doc.containsKey("value")) {
    ok = rtu_write_single(slave, address, doc["value"] | 0);
  } else if (doc.containsKey("values")) {
    JsonArray vals = doc["values"].as<JsonArray>();
    uint16_t qty = vals.size();
    if (qty > 123) {
      request->send(400, "application/json", "{\"success\":false}");
      return;
    }
    uint16_t v[123];
    for (uint16_t i = 0; i < qty; i++) v[i] = vals[i] | 0;
    ok = rtu_write_multiple(slave, address, qty, v);
  }
  
  request->send(ok ? 200 : 500, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}

void initWeb() {
  // Serve different pages based on WiFi configuration state
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (wifi_configured) {
      request->send_P(200, "text/html", HTML_PAGE);
    } else {
      request->send_P(200, "text/html", SETUP_HTML_PAGE);
    }
  });

  webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getStatusJson());
  });

  // WiFi Management APIs
  webServer.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    
    for (int i = 0; i < n; i++) {
      JsonObject network = networks.createNestedObject();
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);
      network["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  webServer.on("/api/wifi/config", HTTP_POST, [](AsyncWebServerRequest *request){}, nullptr, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      String ssid = doc["ssid"] | "";
      String password = doc["password"] | "";
      
      if (ssid.length() == 0) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID required\"}");
        return;
      }
      
      saveWifiConfig(ssid, password);
      request->send(200, "application/json", "{\"success\":true}");
      
      // Reboot after 3 seconds
      delay(3000);
      ESP.restart();
    });

  webServer.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    clearWifiConfig();
    request->send(200, "application/json", "{\"success\":true}");
    delay(1000);
    ESP.restart();
  });

  webServer.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(32768);
    JsonArray arr = doc.createNestedArray("logs");
    for (const auto &s : logBuffer) arr.add(s);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  webServer.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request){
    logBuffer.clear();
    addLog("Logs cleared", "info");
    request->send(200, "application/json", "{\"success\":true}");
  });

  webServer.on("/api/uart/config", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<256> doc;
    doc["baud_rate"] = uartCfg.baud;
    doc["stop_bits"] = uartCfg.stop_bits;
    doc["data_bits"] = uartCfg.data_bits;
    doc["parity"] = String(uartCfg.parity);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  webServer.on("/api/uart/config", HTTP_POST, [](AsyncWebServerRequest *r){}, nullptr, handleUartConfigPost);
  webServer.on("/api/modbus/read", HTTP_POST, [](AsyncWebServerRequest *r){}, nullptr, handleModbusReadPost);
  webServer.on("/api/modbus/write", HTTP_POST, [](AsyncWebServerRequest *r){}, nullptr, handleModbusWritePost);

  webServer.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request){
    factoryReset();
    request->send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  });

  webServer.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    saveStats();
    request->send(200, "application/json", "{\"success\":true}");
    delay(500);
    ESP.restart();
  });

  webServer.begin();
  addLog("Web server started", "info");
}

void initOTA() {
  // Use configuration from top of file
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  
  ArduinoOTA.onStart([]() {
    saveStats();
    addLog("OTA start", "info");
    if (modbusTcpServer) modbusTcpServer->end();
  });
  
  ArduinoOTA.onEnd([]() { 
    addLog("OTA done", "info"); 
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Optional: log progress every 10%
    static unsigned int lastPercent = 0;
    unsigned int percent = (progress * 100) / total;
    if (percent >= lastPercent + 10) {
      addLog("OTA progress: " + String(percent) + "%", "info");
      lastPercent = percent;
    }
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    String errorMsg = "OTA error: ";
    switch (error) {
      case OTA_AUTH_ERROR: errorMsg += "Auth Failed"; break;
      case OTA_BEGIN_ERROR: errorMsg += "Begin Failed"; break;
      case OTA_CONNECT_ERROR: errorMsg += "Connect Failed"; break;
      case OTA_RECEIVE_ERROR: errorMsg += "Receive Failed"; break;
      case OTA_END_ERROR: errorMsg += "End Failed"; break;
      default: errorMsg += "Unknown (" + String(error) + ")"; break;
    }
    addLog(errorMsg, "error");
  });
  
  ArduinoOTA.begin();
  addLog("OTA ready: " + String(OTA_HOSTNAME), "info");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  startMs = millis();

  Serial.println("\n========================================");
  Serial.println("ESP32 Modbus Gateway v1.1.0");
  Serial.println("========================================");

  rtuMutex = xSemaphoreCreateMutex();

  loadConfig();

  if (MODBUS_DE_PIN >= 0) {
    pinMode(MODBUS_DE_PIN, OUTPUT);
    setDE(false);
    addLog("RS485 DE pin: GPIO" + String(MODBUS_DE_PIN), "info");
  }

  uint32_t cfg = SERIAL_8N1;
  if (uartCfg.data_bits == 8) {
    if (uartCfg.parity == 'N') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8N1 : SERIAL_8N2;
    else if (uartCfg.parity == 'E') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8E1 : SERIAL_8E2;
    else if (uartCfg.parity == 'O') cfg = (uartCfg.stop_bits == 1) ? SERIAL_8O1 : SERIAL_8O2;
  }
  
  MODBUS_UART.begin(uartCfg.baud, cfg, MODBUS_RX_PIN, MODBUS_TX_PIN);
  MODBUS_UART.setRxBufferSize(512);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(OTA_HOSTNAME);  // Use OTA hostname for WiFi too
  
  // Try to connect to configured WiFi if available
  if (wifi_configured) {
    addLog("Attempting WiFi connection to: " + wifi_ssid, "info");
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    
    Serial.print("WiFi");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries++ < 30) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      addLog("WiFi: " + WiFi.SSID() + " IP: " + WiFi.localIP().toString(), "success");
      // Disable AP mode after successful WiFi connection for cleaner operation
      WiFi.softAPdisconnect(true);
      addLog("AP mode disabled - connected to WiFi", "info");
    } else {
      addLog("WiFi connection failed - enabling AP mode", "warning");
      WiFi.softAP(AP_SSID, AP_PASS);
      addLog("AP: " + String(AP_SSID) + " IP: " + WiFi.softAPIP().toString(), "info");
    }
  } else {
    addLog("No WiFi configured - starting AP mode", "info");
    WiFi.softAP(AP_SSID, AP_PASS);
    addLog("AP: " + String(AP_SSID) + " IP: " + WiFi.softAPIP().toString(), "info");
    addLog("Connect to '" + String(AP_SSID) + "' to configure WiFi", "info");
  }

  initWeb();

  modbusTcpServer = new AsyncServer(MODBUS_TCP_PORT);
  modbusTcpServer->onClient(onModbusTcpClient, nullptr);
  modbusTcpServer->begin();
  addLog("Modbus TCP: port " + String(MODBUS_TCP_PORT), "info");

  initOTA();

  addLog("Ready! Config stored in NVS.", "success");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWeb UI: http://" + WiFi.localIP().toString());
  } else {
    Serial.println("\nSetup UI: http://" + WiFi.softAPIP().toString());
    Serial.println("Connect to WiFi: " + String(AP_SSID) + " (password: " + String(AP_PASS) + ")");
  }
  Serial.println("OTA: " + String(OTA_HOSTNAME) + ".local");
}

void loop() {
  ArduinoOTA.handle();
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    saveStats();
  }
  
  delay(10);
}
