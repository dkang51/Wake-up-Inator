#include <WiFi.h>
#include <WebServer.h>
#include <Servo.h>

// Network credentials - MUST MATCH monitoring Arduino
const char* WIFI_SSID = "----";
const char* WIFI_PASSWORD = "----";

// Pin definitions
const int STEPPER_PIN1 = 2;      // Servo for blinds control
const int STEPPER_PIN2 = 3;
const int STEPPER_PIN3 = 4;
const int STEPPER_PIN4 = 5;
const int FAN_PIN = 6;          // Relay control for fan
const int BUZZER_PIN = 7;       // Buzzer for alarm
const int HEATER_PIN = 8;       // Relay control for heater

const int STATUS_LED_PIN = 9;   // LED to indicate active connection

// Temperature thresholds (in Fahrenheit) - matching monitoring Arduino
const float SLEEP_TEMP_MAX = 67.0;
const float SLEEP_TEMP_MIN = 60.0;
const float WAKE_TEMP_TARGET = 72.0;

// Servo configuration
const int BLINDS_CLOSED_POS = 0;
const int BLINDS_OPEN_POS = 180;
const int SERVO_STEP = 5;        // Degrees per movement for smooth operation

// Buzzer configuration
const int BUZZER_FREQUENCY = 2000;  // Hz
const int ALARM_DURATION = 500;     // ms
const int ALARM_PAUSE = 500;        // ms

// Message types - MUST MATCH monitoring Arduino
enum MessageType {
  TEMP_ADJUST,
  WAKE_TRIGGER,
  BLINDS_CONTROL,
  ERROR_STATE
};

// State tracking
struct SystemState {
  bool blindsOpen;
  bool alarmActive;
  bool heaterOn;
  bool fanOn;
  float targetTemp;
  unsigned long lastBuzzerToggle;
} state;

Servo blindsServo;
WebServer server(80);

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  // Initialize servo
  blindsServo.attach(SERVO_PIN);
  blindsServo.write(BLINDS_CLOSED_POS);
  state.blindsOpen = false;
  
  // Initialize all systems to off
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  state.heaterOn = false;
  state.fanOn = false;
  state.alarmActive = false;
  state.lastBuzzerToggle = 0;
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    Serial.print(".");
  }
  digitalWrite(STATUS_LED_PIN, HIGH);
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Setup web server endpoints
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
}

void loop() {
  server.handleClient();
  
  if (state.alarmActive) {
    handleAlarm();
  }
}

void handleCommand() {
  if (server.hasArg("plain")) {
    String message = server.arg("plain");
    int separatorIndex = message.indexOf('|');
    
    if (separatorIndex != -1) {
      int messageType = message.substring(0, separatorIndex).toInt();
      String payload = message.substring(separatorIndex + 1);
      
      processCommand(messageType, payload);
    }
  }
  server.send(200, "text/plain", "Command received");
}

void processCommand(int messageType, String payload) {
  Serial.print("Received command type: ");
  Serial.println(messageType);
  Serial.print("Payload: ");
  Serial.println(payload);

  switch (messageType) {
    case TEMP_ADJUST:
      float targetTemp = payload.toFloat();
      if (targetTemp > currentSession.currentTemp) {
        digitalWrite(HEATER_PIN, HIGH);
        digitalWrite(FAN_PIN, LOW);
        state.heaterOn = true;
        state.fanOn = false;
        Serial.println("Heater activated");
      } else {
        digitalWrite(FAN_PIN, HIGH);
        digitalWrite(HEATER_PIN, LOW);
        state.fanOn = true;
        state.heaterOn = false;
        Serial.println("Fan activated");
      }
      break;
      
    case WAKE_TRIGGER:
      startWakeSequence();
      break;
      
    case BLINDS_CONTROL:
      controlBlinds(payload == "OPEN");
      break;
      
    case ERROR_STATE:
      // Handle any errors
      Serial.println("Error state received");
      break;
  }
}

void startWakeSequence() {
  if (!state.alarmActive) {
    state.alarmActive = true;
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("Wake sequence initiated");
  }
}

void handleAlarm() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - state.lastBuzzerToggle >= (digitalRead(BUZZER_PIN) ? ALARM_DURATION : ALARM_PAUSE)) {
    state.lastBuzzerToggle = currentMillis;
    
    if (digitalRead(BUZZER_PIN) == LOW) {
      tone(BUZZER_PIN, BUZZER_FREQUENCY);
    } else {
      noTone(BUZZER_PIN);
    }
  }
}

void controlBlinds(bool open) {
  if (state.blindsOpen != open) {
    if (open) {
      for (int pos = BLINDS_CLOSED_POS; pos <= BLINDS_OPEN_POS; pos += SERVO_STEP) {
        blindsServo.write(pos);
        
      }
    } else {
      for (int pos = BLINDS_OPEN_POS; pos >= BLINDS_CLOSED_POS; pos -= SERVO_STEP) {
        blindsServo.write(pos);
        
      }
    }
    state.blindsOpen = open;
    Serial.println(open ? "Blinds opened" : "Blinds closed");
  }
}
