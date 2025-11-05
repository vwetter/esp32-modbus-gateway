/*
  ESP32 Modbus RTU <-> Modbus TCP Gateway
  With Persistent Configuration Storage
  
  Version: 1.2.2 - Cleanup (remove debug prints & bump version)
  Date: 2025-11-01
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