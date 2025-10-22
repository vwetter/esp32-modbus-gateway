# REST API Reference

Base URL: `http://[gateway-ip]` (e.g., `http://10.0.0.46`)

All POST requests require `Content-Type: application/json`

## Endpoints

### 1. Get System Status

```http
GET /api/status
```

**Response:**
```json
{
  "uptime_s": 8135,
  "wifi_connected": true,
  "wifi_ssid": "MyNetwork",
  "wifi_ip": "10.0.0.46",
  "wifi_rssi": -65,
  "ap_ip": "192.168.4.1",
  "request_count": 1234,
  "error_count": 5,
  "tcp_connections": 3,
  "free_heap": 251584,
  "modbus_baud": 9600
}
```

### 2. Read Modbus Registers

```http
POST /api/modbus/read
```

**Request:**
```json
{
  "slave_id": 1,
  "address": 0,
  "quantity": 10
}
```

**Response (Success):**
```json
{
  "success": true,
  "slave_id": 1,
  "address": 0,
  "values": [100, 200, 300, 400, 500, 600, 700, 800, 900, 1000]
}
```

**Response (Error):**
```json
{
  "success": false,
  "slave_id": 1,
  "address": 0
}
```

**Parameters:**
- `slave_id`: 1-247 (Modbus device address)
- `address`: 0-65535 (starting register)
- `quantity`: 1-125 (number of registers)

### 3. Write Single Register

```http
POST /api/modbus/write
```

**Request:**
```json
{
  "slave_id": 1,
  "address": 100,
  "value": 1234
}
```

**Response:**
```json
{
  "success": true
}
```

### 4. Write Multiple Registers

```http
POST /api/modbus/write
```

**Request:**
```json
{
  "slave_id": 1,
  "address": 100,
  "values": [1234, 5678, 9012]
}
```

### 5. Get Logs

```http
GET /api/logs
```

**Response:**
```json
{
  "logs": [
    "[0s] [info] UART started: 9600 8N1",
    "[5s] [success] Connected to WiFi",
    "[10s] [modbus] TX: 01 03 00 00 00 0A C5 CD"
  ]
}
```

### 6. Clear Logs

```http
POST /api/logs/clear
```

### 7. Get UART Config

```http
GET /api/uart/config
```

**Response:**
```json
{
  "baud_rate": 9600,
  "stop_bits": 1,
  "data_bits": 8,
  "parity": "N"
}
```

### 8. Update UART Config

```http
POST /api/uart/config
```

**Request:**
```json
{
  "baud_rate": 19200,
  "stop_bits": 1,
  "data_bits": 8,
  "parity": "E"
}
```

**Parameters:**
- `baud_rate`: 300-115200
- `stop_bits`: 1 or 2
- `data_bits`: 7 or 8
- `parity`: "N" (none), "E" (even), "O" (odd)

### 9. Restart Device

```http
POST /api/restart
```

### 10. Factory Reset

```http
POST /api/factory-reset

## Code Examples

### cURL

**Read registers:**
```bash
curl -X POST http://10.0.0.46/api/modbus/read \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1,"address":0,"quantity":10}'
```

**Write register:**
```bash
curl -X POST http://10.0.0.46/api/modbus/write \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1,"address":100,"value":1234}'
```

**Get status:**
```bash
curl http://10.0.0.46/api/status | jq
```

### Python

```python
import requests

GATEWAY = "http://10.0.0.46"

def read_registers(slave_id, address, quantity):
    r = requests.post(f"{GATEWAY}/api/modbus/read", json={
        "slave_id": slave_id,
        "address": address,
        "quantity": quantity
    })
    return r.json()

def write_register(slave_id, address, value):
    r = requests.post(f"{GATEWAY}/api/modbus/write", json={
        "slave_id": slave_id,
        "address": address,
        "value": value
    })
    return r.json()

# Example usage
data = read_registers(1, 0, 10)
if data["success"]:
    print(f"Values: {data['values']}")
```

### JavaScript (Node.js)

```javascript
const axios = require('axios');

const GATEWAY = 'http://10.0.0.46';

async function readRegisters(slaveId, address, quantity) {
  const response = await axios.post(`${GATEWAY}/api/modbus/read`, {
    slave_id: slaveId,
    address: address,
    quantity: quantity
  });
  return response.data;
}

// Usage
readRegisters(1, 0, 10)
  .then(data => console.log('Values:', data.values))
  .catch(err => console.error('Error:', err));
```

## Error Handling

All endpoints return HTTP status codes:
- **200**: Success
- **400**: Bad request (invalid parameters)
- **500**: Server error (Modbus transaction failed)

Always check the `success` field in JSON responses.

## Rate Limiting

No built-in rate limiting, but recommended:
- Max 10 requests/second per client
- Allow 100ms between Modbus transactions
- Use connection pooling for multiple requests

## Modbus Function Codes

| Code | Function | Supported |
|------|----------|-----------|
| 01 | Read Coils | ✅ |
| 02 | Read Discrete Inputs | ✅ |
| 03 | Read Holding Registers | ✅ |
| 04 | Read Input Registers | ✅ |
| 05 | Write Single Coil | ✅ |
| 06 | Write Single Register | ✅ |
| 0F | Write Multiple Coils | ✅ |
| 10 | Write Multiple Registers | ✅ |

## Tips

- Cache status requests (update every 2-5 seconds)
- Handle network timeouts (set 5+ second timeout)
- Retry failed requests with exponential backoff
- Check logs endpoint for debugging
