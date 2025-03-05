import serial
import serial.tools.list_ports
import time
from datetime import datetime, timedelta
import usb.core
import usb.util
import usb.backend.libusb1
import os

class iPhoneMonitor:
    # Apple's vendor ID
    APPLE_VENDOR_ID = 0x05ac
    
    # List of common iPhone product IDs
    IPHONE_PRODUCT_IDS = [
        0x12a8, 0x12aa, 0x12ab, 0x12ac, 0x12ad, 0x12ae, 0x12af,
        0x12b0, 0x12b1, 0x12b2, 0x12b3, 0x12b4, 0x12b5, 0x12b6,
        0x12b7, 0x12b8, 0x12b9, 0x12ba,
    ]

    def __init__(self):
        self.arduino = None
        self.phone_connected = True
        self.sleep_start_time = None
        self.backend = None
        self.initialize_usb_backend()
        self.connect_to_arduino()

    def initialize_usb_backend(self):
        """Initialize USB backend"""
        try:
            # Try to find libusb
            possible_paths = [
                '/usr/local/lib/libusb-1.0.dylib',  # Homebrew installation
                '/opt/homebrew/lib/libusb-1.0.dylib',  # M1 Mac Homebrew
                'libusb-1.0.dylib'  # System default
            ]
            
            for path in possible_paths:
                if os.path.exists(path):
                    self.backend = usb.backend.libusb1.get_backend(find_library=lambda x: path)
                    if self.backend:
                        print(f"USB backend initialized using {path}")
                        return
                    
            if not self.backend:
                print("Warning: Could not initialize USB backend. iPhone detection may not work.")
        except Exception as e:
            print(f"Warning: USB backend initialization failed: {e}")

    def connect_to_arduino(self):
        """Auto-detect and connect to Arduino with improved handshaking"""
        print("Available ports:")
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            print(f"- {p.device}: {p.description}")

        KNOWN_PORT = "/dev/cu.usbserial-1120"
        try:
            print(f"\nAttempting to connect to {KNOWN_PORT}...")
            self.arduino = serial.Serial(KNOWN_PORT, 115200, timeout=1)
            time.sleep(2)  # Wait for Arduino to reset
            print("Connected to Arduino!")
            
            print("Waiting for ready signal... (30 second timeout)")
            print("Please reset your ESP32 now if needed...")
            timeout = time.time() + 30
            ready_received = False
            
            while time.time() < timeout and not ready_received:
                if self.arduino.in_waiting:
                    response = self.arduino.readline().decode().strip()
                    print(f"Received: {response}")
                    if response == "READY":
                        # Send acknowledgment
                        print("Sending acknowledgment...")
                        self.arduino.write(b'A')
                        ready_received = True
                        
                        # Wait for confirmation
                        while time.time() < timeout:
                            if self.arduino.in_waiting:
                                response = self.arduino.readline().decode().strip()
                                print(f"Received: {response}")
                                if response == "Connection confirmed":
                                    print("Arduino connection established and verified!")
                                    return
                            time.sleep(0.1)
                print(".", end="", flush=True)
                time.sleep(0.5)
                
            if not ready_received:
                print("\nTimeout waiting for READY signal")
                raise Exception("Arduino connection verification failed")
                
        except Exception as e:
            print(f"Error: {e}")
            raise

    def is_iphone_connected(self):
        """Check specifically for iPhone connection using USB"""
        try:
            if not self.backend:
                return False
                
            # Look for devices with Apple's vendor ID
            devices = usb.core.find(
                idVendor=self.APPLE_VENDOR_ID,
                find_all=True,
                backend=self.backend
            )
            
            if devices:
                devices_list = list(devices)  # Convert generator to list
                for device in devices_list:
                    if device.idProduct in self.IPHONE_PRODUCT_IDS:
                        try:
                            # Try to get device configuration
                            device.get_active_configuration()
                            return True
                        except usb.core.USBError:
                            continue
            return False
            
        except Exception as e:
            print(f"Error checking iPhone connection: {e}")
            return False

    def check_phone_connection(self):
        """Monitor iPhone connection status and handle state changes"""
        current_state = self.is_iphone_connected()

        if not current_state and self.phone_connected:
            # iPhone just disconnected
            self.phone_connected = False
            self.sleep_start_time = datetime.now()
            if self.arduino:
                self.arduino.write(b'0')  # Signal sleep start to Arduino
            print(f"iPhone disconnected at {self.sleep_start_time}")
            
        elif current_state and not self.phone_connected:
            # iPhone just reconnected
            self.phone_connected = True
            if self.arduino:
                self.arduino.write(b'1')  # Signal wake to Arduino
            if self.sleep_start_time:
                duration = datetime.now() - self.sleep_start_time
                print(f"iPhone reconnected. Sleep duration: {duration}")
            self.sleep_start_time = None

    def monitor_arduino_messages(self):
        """Monitor and print Arduino messages"""
        if self.arduino and self.arduino.in_waiting:
            try:
                message = self.arduino.readline().decode().strip()
                if message:
                    print(f"Arduino: {message}")
            except Exception as e:
                print(f"Error reading Arduino message: {e}")

    def run(self):
        """Main monitoring loop"""
        print("Starting iPhone monitoring...")
        try:
            while True:
                try:
                    self.check_phone_connection()
                    self.monitor_arduino_messages()
                    time.sleep(1)  # Check every second
                except Exception as e:
                    print(f"Error in monitoring loop: {e}")
                    time.sleep(1)  # Wait before retrying
                
        except KeyboardInterrupt:
            print("\nMonitoring stopped by user")
        finally:
            if self.arduino:
                try:
                    self.arduino.close()
                    print("Arduino connection closed")
                except Exception as e:
                    print(f"Error closing Arduino connection: {e}")

def main():
    print("Starting iPhone Monitor...")
    print("Checking requirements...")
    
    try:
        monitor = iPhoneMonitor()
        monitor.run()
    except Exception as e:
        print(f"Error: {e}")
    finally:
        input("\nPress Enter to exit...")

if __name__ == "__main__":
    main()