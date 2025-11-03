#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define VRX_MOVE_PIN 35
#define VRY_MOVE_PIN 34
#define SW_PIN 12

// ADC and deadzone
const int DEADZONE = 150; // ADC units
const int ADC_MAX = 4095;

// Send timing
unsigned long lastSendMs = 0;
const unsigned long minSendInterval = 100; // ms

// Receiver MAC (replace with your receiver MAC)
uint8_t peerMac[] = { 0x20, 0xE7, 0xC8, 0x68, 0xB8, 0x30 };

char lastSent = 0;

// Calibration storage
int centerX = 0;
int centerY = 0;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send to ");
  for (int i = 0; i < 6; ++i) {
    if (i) Serial.print(":");
    Serial.print(mac_addr[i], HEX);
  }
  Serial.print(" status=");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "ERR");
}

void calibrateCenter() {
  long sumX = 0, sumY = 0;
  const int samples = 20;
  for (int i = 0; i < samples; ++i) {
    sumX += analogRead(VRX_MOVE_PIN);
    sumY += analogRead(VRY_MOVE_PIN);
    delay(20);
  }
  centerX = sumX / samples;
  centerY = sumY / samples;
  Serial.print("Calibrated centerX=");
  Serial.print(centerX);
  Serial.print(" centerY=");
  Serial.println(centerY);
}

void sendCmd(char cmd, bool force = false) {
  unsigned long now = millis();
  if (!force) {
    if (cmd == lastSent && (now - lastSendMs) < 1000) return;
    if ((now - lastSendMs) < minSendInterval) return;
  }
  lastSent = cmd;
  lastSendMs = now;

  esp_err_t result = esp_now_send(peerMac, (uint8_t*)&cmd, 1);
  if (result != ESP_OK) {
    Serial.print("esp_now_send err: ");
    Serial.println(result);
  } else {
    Serial.print("Sent: ");
    Serial.println(cmd);
  }
}

void setupEspNowPeer() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(peerInfo.peer_addr) == false) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
    }
  }
  Serial.println("ESP-NOW peer added");
}

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(SW_PIN, INPUT_PULLUP);

  // ADC attenuation for full scale
  analogSetPinAttenuation(VRX_MOVE_PIN, ADC_11db);
  analogSetPinAttenuation(VRY_MOVE_PIN, ADC_11db);

  calibrateCenter();

  setupEspNowPeer();
}

void loop() {
  int valueMoveY = analogRead(VRY_MOVE_PIN);
  int valueMoveX = analogRead(VRX_MOVE_PIN);
  int swState = digitalRead(SW_PIN);

  if (swState == LOW) {
    sendCmd('S', true);
    delay(80);
    return;
  }

  int dx = valueMoveX - centerX;
  int dy = valueMoveY - centerY;

  if (abs(dx) < DEADZONE) dx = 0;
  if (abs(dy) < DEADZONE) dy = 0;

  char cmd = 'S';

  // tune thresholds relative to center
  const int FORWARD_OFFSET = -700;
  const int BACKWARD_OFFSET = 700;
  const int LEFT_OFFSET = -600;
  const int RIGHT_OFFSET = 600;

  if (dy < FORWARD_OFFSET) cmd = 'F';
  else if (dy > BACKWARD_OFFSET) cmd = 'B';

  // only turn when not going forward/back
  if (dx < LEFT_OFFSET && cmd == 'S') cmd = 'L';
  else if (dx > RIGHT_OFFSET && cmd == 'S') cmd = 'R';

  sendCmd(cmd);

  delay(80);
}
