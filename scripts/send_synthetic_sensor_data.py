#!/usr/bin/env python3
"""
Synthetic Sensor Data Sender for Naptick API

Continuously sends synthetic sensor data to the Naptick API endpoint
in the background. Useful for testing and development.

Usage:
    python3 send_synthetic_sensor_data.py [--device-id DEVICE_ID] [--interval SECONDS] [--daemon]
    
    --device-id: Device ID to use (default: auto-generated)
    --interval:  Send interval in seconds (default: 60)
    --daemon:    Run as background daemon
"""

import argparse
import json
import random
import time
import sys
import os
import signal
from datetime import datetime, timezone
from typing import Dict, Any
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

# Configuration
NAPTICK_API_URL = "https://api-uat.naptick.com/sensor-service/sensor-service/stream"
DEFAULT_INTERVAL = 60  # seconds
DEFAULT_DEVICE_ID = "synthetic-device-001"

# PID file for daemon mode
PID_FILE = "/tmp/naptick_sensor_sender.pid"

# Sensor value ranges (realistic ranges for indoor environment)
SENSOR_RANGES = {
    "temperature": (18.0, 28.0),      # Celsius
    "humidity": (30.0, 70.0),          # Percentage
    "co2": (400, 1200),                # PPM
    "pm1_0": (0, 50),                  # ug/m3
    "pm2_5": (0, 75),                  # ug/m3
    "pm10": (0, 100),                  # ug/m3
    "voc": (0, 500),                   # Index
    "light": (0, 1000),                # Lux
    "uv_index": (0, 11),               # UV Index
    "ambient_lux": (0, 2000)           # Lux
}

# Trend simulation (for more realistic data)
class SensorTrend:
    def __init__(self, min_val: float, max_val: float, initial: float = None):
        self.min_val = min_val
        self.max_val = max_val
        self.current = initial if initial is not None else (min_val + max_val) / 2
        self.velocity = 0.0
        self.target = self.current
        
    def update(self) -> float:
        # Random walk with momentum
        if abs(self.current - self.target) < 0.1:
            # Pick new target
            self.target = random.uniform(self.min_val, self.max_val)
            self.velocity = random.uniform(-0.5, 0.5)
        else:
            # Move toward target with some randomness
            direction = 1 if self.target > self.current else -1
            self.velocity = self.velocity * 0.9 + direction * random.uniform(0.1, 0.5)
            self.current += self.velocity
            
        # Clamp to range
        self.current = max(self.min_val, min(self.max_val, self.current))
        return self.current

class SyntheticSensorSender:
    def __init__(self, device_id: str, interval: int, api_url: str = NAPTICK_API_URL):
        self.device_id = device_id
        self.interval = interval
        self.api_url = api_url
        self.running = True
        self.session = None
        self.sensor_trends = {}
        
        # Initialize sensor trends
        for sensor, (min_val, max_val) in SENSOR_RANGES.items():
            self.sensor_trends[sensor] = SensorTrend(min_val, max_val)
        
        # Setup HTTP session with retries
        self.session = requests.Session()
        retry_strategy = Retry(
            total=3,
            backoff_factor=1,
            status_forcelist=[429, 500, 502, 503, 504],
        )
        adapter = HTTPAdapter(max_retries=retry_strategy)
        self.session.mount("http://", adapter)
        self.session.mount("https://", adapter)
        
        # Setup signal handlers for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals gracefully"""
        print(f"\nReceived signal {signum}, shutting down...")
        self.running = False
    
    def generate_sensor_data(self) -> Dict[str, Any]:
        """Generate synthetic sensor data with realistic trends"""
        sensors = {}
        
        # Update trends and generate values
        sensors["temperature"] = round(self.sensor_trends["temperature"].update(), 1)
        sensors["humidity"] = round(self.sensor_trends["humidity"].update(), 1)
        sensors["co2"] = int(self.sensor_trends["co2"].update())
        sensors["pm1_0"] = round(self.sensor_trends["pm1_0"].update(), 1)
        sensors["pm2_5"] = round(self.sensor_trends["pm2_5"].update(), 1)
        sensors["pm10"] = round(self.sensor_trends["pm10"].update(), 1)
        sensors["voc"] = int(self.sensor_trends["voc"].update())
        sensors["light"] = round(self.sensor_trends["light"].update(), 1)
        sensors["uv_index"] = round(self.sensor_trends["uv_index"].update(), 1)
        sensors["ambient_lux"] = round(self.sensor_trends["ambient_lux"].update(), 1)
        
        return sensors
    
    def format_iso8601_timestamp(self) -> str:
        """Generate ISO 8601 timestamp"""
        return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    
    def send_sensor_data(self, sensors: Dict[str, Any]) -> bool:
        """Send sensor data to Naptick API"""
        payload = {
            "deviceId": self.device_id,
            "timestamp": self.format_iso8601_timestamp(),
            "sensors": sensors
        }
        
        try:
            response = self.session.post(
                self.api_url,
                json=payload,
                headers={"Content-Type": "application/json"},
                timeout=10
            )
            
            if response.status_code == 200:
                print(f"✅ Sent sensor data: {json.dumps(payload, indent=2)}")
                return True
            else:
                print(f"❌ API error {response.status_code}: {response.text}")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f"❌ Request failed: {e}")
            return False
    
    def run(self):
        """Main loop - continuously send sensor data"""
        print(f"🚀 Starting synthetic sensor data sender")
        print(f"   Device ID: {self.device_id}")
        print(f"   API URL: {self.api_url}")
        print(f"   Interval: {self.interval} seconds")
        print(f"   Press Ctrl+C to stop\n")
        
        iteration = 0
        success_count = 0
        error_count = 0
        
        while self.running:
            try:
                iteration += 1
                print(f"[{iteration}] Generating sensor data...")
                
                # Generate synthetic data
                sensors = self.generate_sensor_data()
                
                # Send to API
                if self.send_sensor_data(sensors):
                    success_count += 1
                else:
                    error_count += 1
                
                # Print statistics
                if iteration % 10 == 0:
                    print(f"\n📊 Statistics: {success_count} successful, {error_count} errors\n")
                
                # Wait for next interval
                if self.running:
                    time.sleep(self.interval)
                    
            except KeyboardInterrupt:
                self.running = False
                break
            except Exception as e:
                print(f"❌ Unexpected error: {e}")
                error_count += 1
                if self.running:
                    time.sleep(self.interval)
        
        print(f"\n📊 Final Statistics: {success_count} successful, {error_count} errors")
        print("👋 Shutting down...")
        
        if self.session:
            self.session.close()

def daemonize():
    """Fork process to run as daemon"""
    try:
        pid = os.fork()
        if pid > 0:
            # Parent process - exit
            sys.exit(0)
    except OSError as e:
        print(f"Fork failed: {e}")
        sys.exit(1)
    
    # Child process - detach from terminal
    os.chdir("/")
    os.setsid()
    os.umask(0)
    
    # Second fork
    try:
        pid = os.fork()
        if pid > 0:
            sys.exit(0)
    except OSError as e:
        print(f"Second fork failed: {e}")
        sys.exit(1)
    
    # Write PID file
    with open(PID_FILE, 'w') as f:
        f.write(str(os.getpid()))
    
    # Redirect standard file descriptors
    sys.stdout.flush()
    sys.stderr.flush()
    si = open(os.devnull, 'r')
    so = open(os.devnull, 'a+')
    se = open(os.devnull, 'a+')
    os.dup2(si.fileno(), sys.stdin.fileno())
    os.dup2(so.fileno(), sys.stdout.fileno())
    os.dup2(se.fileno(), sys.stderr.fileno())

def main():
    parser = argparse.ArgumentParser(
        description="Send synthetic sensor data to Naptick API",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run with default settings (60s interval)
  python3 send_synthetic_sensor_data.py
  
  # Run with custom device ID and interval
  python3 send_synthetic_sensor_data.py --device-id my-device-123 --interval 30
  
  # Run as background daemon
  python3 send_synthetic_sensor_data.py --daemon
  
  # Stop daemon
  kill $(cat /tmp/naptick_sensor_sender.pid)
        """
    )
    
    parser.add_argument(
        "--device-id",
        type=str,
        default=DEFAULT_DEVICE_ID,
        help=f"Device ID (default: {DEFAULT_DEVICE_ID})"
    )
    
    parser.add_argument(
        "--interval",
        type=int,
        default=DEFAULT_INTERVAL,
        help=f"Send interval in seconds (default: {DEFAULT_INTERVAL})"
    )
    
    parser.add_argument(
        "--daemon",
        action="store_true",
        help="Run as background daemon"
    )
    
    parser.add_argument(
        "--api-url",
        type=str,
        default=NAPTICK_API_URL,
        help=f"API endpoint URL (default: {NAPTICK_API_URL})"
    )
    
    args = parser.parse_args()
    
    # Run as daemon if requested
    if args.daemon:
        daemonize()
    
    # Create and run sender
    sender = SyntheticSensorSender(
        device_id=args.device_id,
        interval=args.interval,
        api_url=args.api_url
    )
    
    sender.run()

if __name__ == "__main__":
    main()
