#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <time.h>

// Network credentials
const char* WIFI_SSID = "----";
const char* WIFI_PASSWORD = "----";

// Task execution Arduino IP and port
const char* TASK_ARDUINO_IP = "192.168.1.101";
const int TASK_ARDUINO_PORT = 80;

// Pin definitions
const int TEMP_SENSOR_PIN = 4;     // DHT sensor pin
const int LED_INDICATOR_PIN = 9;    // LED to indicate active monitoring

// Time and mode setting constants
const char TIME_SET_PREFIX = 't';   // Prefix for time setting commands
const int TIME_COMMAND_LENGTH = 5;  // 't' plus HHMM
const char STRICT_CHAR = 'S';
const char FLEXIBLE_CHAR = 'F';
bool alarmModeSet = false;

// DHT sensor setup
DHT dht(TEMP_SENSOR_PIN, DHT22);

// Temperature thresholds (in Fahrenheit)
const float SLEEP_TEMP_MAX = 67.0;
const float SLEEP_TEMP_MIN = 60.0;
const float WAKE_TEMP_TARGET = 72.0;

// Time constants (in milliseconds)
const unsigned long REM_CYCLE_DURATION = 5400000;  // 90 minutes
const unsigned long BUFFER_TIME = 1200000;         // 20 minutes
const unsigned long TEMP_CHECK_INTERVAL = 60000;   // 1 minute
const unsigned long WAKE_TEMP_PREP_TIME = 1800000; // 30 minutes
const unsigned long MESSAGE_TIMEOUT = 5000;        // 5 seconds
const unsigned long RETRY_DELAY = 1000;            // 1 second
const unsigned long HANDSHAKE_TIMEOUT = 30000;     // 30 seconds for handshake

// Message types
enum MessageType {
  TEMP_ADJUST,
  WAKE_TRIGGER,
  BLINDS_CONTROL,
  ERROR_STATE
};

// Sleep tracking structure
struct SleepSession {
  bool isActive;
  bool isStrictMode;
  unsigned long startTime;
  unsigned long targetWakeTime;
  unsigned long calculatedWakeTime;
  unsigned long phoneOffDuration;
  float currentTemp;
  bool tempControlActive;
  bool validSession;
} currentSession;

WiFiClient client;
WebServer server(80);

// Function to get current time in seconds since midnight
unsigned long getCurrentTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  }
  return timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
}

// Convert HHMM to seconds since midnight
unsigned long timeToSeconds(int hours, int minutes) {
  return (hours * 3600UL) + (minutes * 60UL);
}

// Modified abs function for unsigned long
unsigned long ulabs(unsigned long a, unsigned long b) {
  return (a > b) ? (a - b) : (b - a);
}

void waitForPythonAcknowledgment() {
  unsigned long startTime = millis();
  while (millis() - startTime < HANDSHAKE_TIMEOUT) {
    if (Serial.available()) {
      char ack = Serial.read();
      if (ack == 'A') {
        Serial.println("Connection confirmed");
        return;
      }
    }
    
  }
  Serial.println("Warning: No acknowledgment received from Python script");
}

unsigned long calculateOptimalWakeUpTime(unsigned long startTime, unsigned long targetTime, bool isStrict) {
  unsigned long totalSleepTime = targetTime - startTime - 600; // Subtract 10 minutes for falling asleep

  if (isStrict) {
    // For strict mode - same as before
    if (totalSleepTime <= 0) {
      return targetTime;
    }

    unsigned long maxREMCycles = totalSleepTime / (REM_CYCLE_DURATION / 1000);
    unsigned long basicWakeTime = startTime + (maxREMCycles * (REM_CYCLE_DURATION / 1000));
    
    if (basicWakeTime == targetTime) {
      return targetTime;
    }
    
    unsigned long timeToTarget = targetTime - basicWakeTime;
    
    if (timeToTarget <= 1800) { // 30 minutes in seconds
      return basicWakeTime + timeToTarget;
    }
    
    return basicWakeTime + 1800;
  } else {
    // Flexible mode - corrected calculation
    // Calculate maximum number of complete REM cycles that fit
    unsigned long maxREMCycles = totalSleepTime / (REM_CYCLE_DURATION / 1000);
    
    // Calculate two potential wake times:
    // Option 1: Max REM cycles + buffer (like strict mode)
    unsigned long option1 = startTime + (maxREMCycles * (REM_CYCLE_DURATION / 1000));
    if (option1 < targetTime) {
      option1 += 1800; // Add up to 30 minutes buffer
    }
    
    // Option 2: Max REM cycles + 1 additional cycle
    unsigned long option2 = startTime + ((maxREMCycles + 1) * (REM_CYCLE_DURATION / 1000));
    
    // Calculate which option is closer to target time
    unsigned long diff1 = ulabs(targetTime, option1);
    unsigned long diff2 = ulabs(targetTime, option2);
    
    // Return the option closest to target time
    return (diff1 <= diff2) ? option1 : option2;
  }
}

void checkPhoneState() {
  static char timeBuffer[TIME_COMMAND_LENGTH + 1];
  static int bufferIndex = 0;
  
  while (Serial.available() > 0) {
    char inChar = Serial.read();
    
    // Handle time setting command
    if (inChar == TIME_SET_PREFIX) {
      // Start new time input
      bufferIndex = 0;
      timeBuffer[bufferIndex++] = inChar;
    }
    else if (bufferIndex > 0 && bufferIndex < TIME_COMMAND_LENGTH) {
      // Continue collecting time input
      timeBuffer[bufferIndex++] = inChar;
      
      // Process complete time input
      if (bufferIndex == TIME_COMMAND_LENGTH) {
        timeBuffer[bufferIndex] = '\0';
        
        // Extract hours and minutes
        int hours = (timeBuffer[1] - '0') * 10 + (timeBuffer[2] - '0');
        int minutes = (timeBuffer[3] - '0') * 10 + (timeBuffer[4] - '0');
        
        // Validate input
        if (hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60) {
          unsigned long newWakeTime = timeToSeconds(hours, minutes);
          currentSession.targetWakeTime = newWakeTime;
          
          // Print target wake time
          Serial.print("\nTarget wake time set to ");
          if (hours < 10) Serial.print("0");
          Serial.print(hours);
          Serial.print(":");
          if (minutes < 10) Serial.print("0");
          Serial.println(minutes);
          
          // Prompt for alarm mode
          Serial.println("Please select alarm mode:");
          Serial.println("Enter 'S' for Strict mode (exact time or earlier)");
          Serial.println("Enter 'F' for Flexible mode (optimize for sleep cycles)");
          alarmModeSet = false;
        } else {
          Serial.println("Invalid time format. Use tHHMM (e.g., t0700 for 7:00 AM)");
        }
        bufferIndex = 0;
      }
    }
    // Handle alarm mode selection
    else if (!alarmModeSet && (inChar == STRICT_CHAR || inChar == FLEXIBLE_CHAR)) {
    currentSession.isStrictMode = (inChar == STRICT_CHAR);
    alarmModeSet = true;
    Serial.println(inChar == STRICT_CHAR ? "\nStrict mode selected" : "\nFlexible mode selected");
    
    // Calculate wake time as soon as mode is selected
    unsigned long currentTime = getCurrentTime();
    currentSession.calculatedWakeTime = calculateOptimalWakeUpTime(
        currentTime,
        currentSession.targetWakeTime,
        currentSession.isStrictMode
    );
    
    // Display calculated alarm time
    unsigned long wakeHours = (currentSession.calculatedWakeTime / 3600) % 24;
    unsigned long wakeMinutes = (currentSession.calculatedWakeTime % 3600) / 60;
    
    Serial.print("Alarm set for: ");
    if (wakeHours < 10) Serial.print("0");
    Serial.print(wakeHours);
    if (wakeMinutes < 10) Serial.print("0");
    Serial.println(wakeMinutes);
    }
    // Handle phone state signals
    else if (inChar == '0' && !currentSession.isActive) {
      if (currentSession.targetWakeTime == 0) {
        Serial.println("Please set wake time first using tHHMM format");
        return;
      }
      if (!alarmModeSet) {
        Serial.println("Please select alarm mode (S/F) first");
        return;
      }
      
      currentSession.isActive = true;
      currentSession.validSession = true;
      currentSession.startTime = getCurrentTime();
      
      currentSession.calculatedWakeTime = calculateOptimalWakeUpTime(
        currentSession.startTime,
        currentSession.targetWakeTime,
        currentSession.isStrictMode
      );
      
      // Display all relevant times
      unsigned long startHours = (currentSession.startTime / 3600) % 24;
      unsigned long startMinutes = (currentSession.startTime % 3600) / 60;
      unsigned long targetHours = (currentSession.targetWakeTime / 3600) % 24;
      unsigned long targetMinutes = (currentSession.targetWakeTime % 3600) / 60;
      unsigned long wakeHours = (currentSession.calculatedWakeTime / 3600) % 24;
      unsigned long wakeMinutes = (currentSession.calculatedWakeTime % 3600) / 60;
      
      Serial.println("\n=== Sleep Session Started ===");
      Serial.print("Sleep start time: ");
      if (startHours < 10) Serial.print("0");
      Serial.print(startHours);
      Serial.print(":");
      if (startMinutes < 10) Serial.print("0");
      Serial.println(startMinutes);
      
      Serial.print("Target wake time: ");
      if (targetHours < 10) Serial.print("0");
      Serial.print(targetHours);
      Serial.print(":");
      if (targetMinutes < 10) Serial.print("0");
      Serial.println(targetMinutes);
      
      Serial.print("Calculated wake time: ");
      if (wakeHours < 10) Serial.print("0");
      Serial.print(wakeHours);
      Serial.print(":");
      if (wakeMinutes < 10) Serial.print("0");
      Serial.println(wakeMinutes);
      
      if (currentSession.isStrictMode) {
        Serial.println("Mode: Strict (will wake at or before target time)");
      } else {
        Serial.println("Mode: Flexible (optimized for sleep cycles)");
      }
      Serial.println("===========================");
    }
    else if (inChar == '1' && currentSession.isActive) {
      currentSession.isActive = false;
      currentSession.validSession = false;
      Serial.println("\nSleep session ended");
    }
  }
}

void monitorTemperature() {
  static unsigned long lastTempCheck = 0;
  
  if (millis() - lastTempCheck >= TEMP_CHECK_INTERVAL) {
    float temperature = dht.readTemperature(true); // Read in Fahrenheit
    
    if (!isnan(temperature)) {
      currentSession.currentTemp = temperature;
      
      if (currentSession.isActive) {
        unsigned long currentTime = getCurrentTime();
        unsigned long timeToWake = currentSession.calculatedWakeTime - currentTime;
        
        if (timeToWake <= WAKE_TEMP_PREP_TIME / 1000) {
          // Start warming up the room
          if (temperature < WAKE_TEMP_TARGET) {
            sendMessage(TEMP_ADJUST, String(WAKE_TEMP_TARGET));
          }
        } else {
          // Maintain sleep temperature
          if (temperature > SLEEP_TEMP_MAX) {
            sendMessage(TEMP_ADJUST, String(SLEEP_TEMP_MAX));
          } else if (temperature < SLEEP_TEMP_MIN) {
            sendMessage(TEMP_ADJUST, String(SLEEP_TEMP_MIN));
          }
        }
      }
    }
    
    lastTempCheck = millis();
  }
}

void handleWakeUpSequence() {
  if (currentSession.isActive && getCurrentTime() >= currentSession.calculatedWakeTime) {
    // Trigger wake-up sequence
    sendMessage(WAKE_TRIGGER, "START");
    sendMessage(BLINDS_CONTROL, "OPEN");
    
    // End sleep session
    currentSession.isActive = false;
    Serial.println("Wake-up sequence triggered");
  }
}

void setup() {
  Serial.begin(115200);
  
  
  // Initialize pins
  pinMode(LED_INDICATOR_PIN, OUTPUT);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    
    digitalWrite(LED_INDICATOR_PIN, !digitalRead(LED_INDICATOR_PIN));
    Serial.print(".");
  }
  digitalWrite(LED_INDICATOR_PIN, HIGH);
  Serial.println("\nWiFi Connected!");
  
  // Initialize time
  configTime(0, 0, "pool.ntp.org");
  
  // Initialize sleep session
  currentSession.isActive = false;
  currentSession.tempControlActive = false;
  currentSession.targetWakeTime = 0;
  currentSession.validSession = false;
  alarmModeSet = false;
  
  // Send ready signal and wait for acknowledgment
  Serial.println("READY");
  waitForPythonAcknowledgment();
}

void loop() {
  server.handleClient();
  
  // Check for phone state updates and time settings
  checkPhoneState();
  
  // Monitor and control temperature
  monitorTemperature();
  
  // Handle wake-up sequence if needed
  if (currentSession.isActive) {
    handleWakeUpSequence();
  }
}

void sendMessage(MessageType type, String payload) {
  if (client.connect(TASK_ARDUINO_IP, TASK_ARDUINO_PORT)) {
    String message = String(type) + "|" + payload;
    client.println("POST /command HTTP/1.1");
    client.println("Host: " + String(TASK_ARDUINO_IP));
    client.println("Content-Type: text/plain");
    client.println("Content-Length: " + String(message.length()));
    client.println();
    client.println(message);
    
    if (!client.connected()) {
      client.stop();
    }
  }
}
