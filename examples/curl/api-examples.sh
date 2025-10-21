#!/bin/bash
# ESP32 Modbus Gateway - API Examples using curl

GATEWAY="http://10.0.0.46"

echo "ESP32 Modbus Gateway - API Examples"
echo "===================================="
echo

# 1. Get system status
echo "1. Getting system status..."
curl -s "$GATEWAY/api/status" | jq
echo
echo "---"
echo

# 2. Read registers
echo "2. Reading 10 registers from slave 1, address 0..."
curl -s -X POST "$GATEWAY/api/modbus/read" \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1,"address":0,"quantity":10}' | jq
echo
echo "---"
echo

# 3. Write single register
echo "3. Writing value 1234 to slave 1, address 100..."
curl -s -X POST "$GATEWAY/api/modbus/write" \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1,"address":100,"value":1234}' | jq
echo
echo "---"
echo

# 4. Write multiple registers
echo "4. Writing multiple values to slave 1, address 200..."
curl -s -X POST "$GATEWAY/api/modbus/write" \
  -H "Content-Type: application/json" \
  -d '{"slave_id":1,"address":200,"values":[100,200,300,400,500]}' | jq
echo
echo "---"
echo

# 5. Get UART config
echo "5. Getting UART configuration..."
curl -s "$GATEWAY/api/uart/config" | jq
echo
echo "---"
echo

# 6. Get logs (last 10)
echo "6. Getting system logs (last 10 entries)..."
curl -s "$GATEWAY/api/logs" | jq '.logs[-10:]'
echo

echo "Done!"
