#!/usr/bin/env python3
"""
Simple example: Read Modbus registers via ESP32 gateway
"""

import requests
import sys

# Gateway configuration
GATEWAY_IP = "10.0.0.46"  # Change to your gateway IP
BASE_URL = f"http://{GATEWAY_IP}"

def read_registers(slave_id, address, quantity):
    """Read holding registers from Modbus device"""
    url = f"{BASE_URL}/api/modbus/read"
    
    payload = {
        "slave_id": slave_id,
        "address": address,
        "quantity": quantity
    }
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        response.raise_for_status()
        data = response.json()
        
        if data["success"]:
            print(f"✅ Read successful!")
            print(f"   Slave ID: {data['slave_id']}")
            print(f"   Starting Address: {data['address']}")
            print(f"   Values:")
            for i, value in enumerate(data['values']):
                addr = data['address'] + i
                print(f"      [{addr}] = {value} (0x{value:04X})")
            return data['values']
        else:
            print(f"❌ Read failed!")
            print(f"   Check logs: {BASE_URL}/api/logs")
            return None
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Connection error: {e}")
        print(f"   Is gateway at {GATEWAY_IP}?")
        return None

if __name__ == "__main__":
    # Example: Read 10 registers from slave 1, starting at address 0
    
    if len(sys.argv) == 4:
        slave = int(sys.argv[1])
        addr = int(sys.argv[2])
        qty = int(sys.argv[3])
    else:
        # Defaults
        slave = 1
        addr = 0
        qty = 10
        print(f"Usage: {sys.argv[0]} <slave_id> <address> <quantity>")
        print(f"Using defaults: slave={slave}, address={addr}, quantity={qty}\n")
    
    read_registers(slave, addr, qty)
