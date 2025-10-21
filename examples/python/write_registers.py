#!/usr/bin/env python3
"""
Simple example: Write Modbus registers via ESP32 gateway
"""

import requests
import sys

GATEWAY_IP = "10.0.0.46"
BASE_URL = f"http://{GATEWAY_IP}"

def write_single_register(slave_id, address, value):
    """Write single holding register"""
    url = f"{BASE_URL}/api/modbus/write"
    
    payload = {
        "slave_id": slave_id,
        "address": address,
        "value": value
    }
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        data = response.json()
        
        if data["success"]:
            print(f"✅ Write successful!")
            print(f"   Wrote {value} (0x{value:04X}) to address {address}")
            return True
        else:
            print(f"❌ Write failed!")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Error: {e}")
        return False

def write_multiple_registers(slave_id, address, values):
    """Write multiple holding registers"""
    url = f"{BASE_URL}/api/modbus/write"
    
    payload = {
        "slave_id": slave_id,
        "address": address,
        "values": values
    }
    
    try:
        response = requests.post(url, json=payload, timeout=5)
        data = response.json()
        
        if data["success"]:
            print(f"✅ Write successful!")
            print(f"   Wrote {len(values)} values starting at {address}")
            return True
        else:
            print(f"❌ Write failed!")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <slave_id> <address> <value> [value2] [value3] ...")
        print(f"\nExample: {sys.argv[0]} 1 100 1234")
        print(f"         {sys.argv[0]} 1 100 1234 5678 9012")
        sys.exit(1)
    
    slave = int(sys.argv[1])
    addr = int(sys.argv[2])
    values = [int(v) for v in sys.argv[3:]]
    
    if len(values) == 1:
        write_single_register(slave, addr, values[0])
    else:
        write_multiple_registers(slave, addr, values)
