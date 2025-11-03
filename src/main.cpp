#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define VRX_MOVE_PIN 35
#define VRY_MOVE_PIN 34
#define SW_PIN 12

// Sampling & smoothing
const int MEDIAN_WINDOW = 5;   // must be odd
const int AVG_SAMPLES = 5;
const int SAMPLE_DELAY_MS = 4;
int xBuf[MEDIAN_WINDOW];
int yBuf[MEDIAN_WINDOW];

// Spike and ADC
const int ADC_MAX = 4095;
const int SPIKE_DELTA = 1000;  // if a reading jumps > this to 0/4095, treat as spike

// Deadzone & thresholds
const int DEADZONE = 260;         // ignore small noise
const int HORIZ_MIN = 400;        // minimum dx magnitude to consider horizontal significant
const int VERT_MIN  = 400;        // minimum dy magnitude to consider vertical significant
const int DOM_TOLERANCE = 50;     // small allowance so near-ties prefer the intended axis

const int FORWARD_OFFSET = -700;
const int BACKWARD_OFFSET = 700;
const int LEFT_OFFSET = -600;
const int RIGHT_OFFSET = 600;
const int Y_ALLOW_FOR_TURN = 220; // fallback small band to allow turns

// Timing
unsigned long lastSendMs = 0;
const unsigned long minSendInterval = 80;
char lastSent = 0;

// Receiver MAC (your device)
uint8_t peerMac[] = { 0x20, 0xE7, 0xC8, 0x68, 0xB8, 0x30 };

// Center calibration
int centerX = 0;
int centerY = 0;

// Utility: median of small array
int medianOfArray(int *arr, int size) {
  int tmp[MEDIAN_WINDOW];
  for (int i = 0; i < size; ++i) tmp[i] = arr[i];
  for (int i = 0; i < size - 1; ++i)
    for (int j = i + 1; j < size; ++j)
      if (tmp[j] < tmp[i]) { int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
  return tmp[size / 2];
}

void printMac(const uint8_t *mac) {
  for (int i = 0; i < 6; ++i) {
    if (i) Serial.print(":");
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send to ");
  printMac(mac_addr);
  //Serial.print(" status=");
  //Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "ERR");
}

bool ensureEspNowPeer() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  if (esp_now_init() != ESP_OK) {
    esp_now_deinit();
    delay(50);
    if (esp_now_init() != ESP_OK) return false;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(peerInfo.peer_addr)) esp_now_del_peer(peerInfo.peer_addr);
  return (esp_now_add_peer(&peerInfo) == ESP_OK);
}

void calibrateCenter() {
  long sumX = 0, sumY = 0;
  const int samples = 30;
  Serial.println("Calibrating center - keep joystick idle");
  for (int i = 0; i < samples; ++i) {
    sumX += analogRead(VRX_MOVE_PIN);
    sumY += analogRead(VRY_MOVE_PIN);
    delay(12);
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
  esp_err_t res = esp_now_send(peerMac, (uint8_t*)&cmd, 1);
  if (res == ESP_OK) Serial.print("Sent: "), Serial.println(cmd);
  else Serial.print("esp_now_send err: "), Serial.println(res);
}

// read sensor: avg then median smoothing + simple spike reject
int readFilteredAxis(int pin, int *buf) {
  long sum = 0;
  for (int i = 0; i < AVG_SAMPLES; ++i) {
    sum += analogRead(pin);
    delay(SAMPLE_DELAY_MS);
  }
  int avg = sum / AVG_SAMPLES;

  // shift buffer and append
  for (int i = 0; i < MEDIAN_WINDOW - 1; ++i) buf[i] = buf[i + 1];
  buf[MEDIAN_WINDOW - 1] = avg;

  int med = medianOfArray(buf, MEDIAN_WINDOW);
  int prev = buf[MEDIAN_WINDOW - 2];

  // ignore single-sample saturations if they jump too much
  if ((med == 0 || med == ADC_MAX) && abs(med - prev) > SPIKE_DELTA) {
    return prev;
  }
  return med;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(SW_PIN, INPUT_PULLUP);

  analogSetPinAttenuation(VRX_MOVE_PIN, ADC_11db);
  analogSetPinAttenuation(VRY_MOVE_PIN, ADC_11db);

  calibrateCenter();
  for (int i = 0; i < MEDIAN_WINDOW; ++i) {
    xBuf[i] = centerX;
    yBuf[i] = centerY;
  }

  if (!ensureEspNowPeer()) Serial.println("ESP-NOW init failed (will retry on send)");
  else {
    Serial.print("ESP-NOW peer ready: ");
    printMac(peerMac);
    Serial.println();
  }
  Serial.println("Ready");
}

void loop() {
  int valueMoveX = readFilteredAxis(VRX_MOVE_PIN, xBuf);
  int valueMoveY = readFilteredAxis(VRY_MOVE_PIN, yBuf);
  int swState = digitalRead(SW_PIN);

  if (swState == LOW) {
    sendCmd('S', true);
    delay(90);
    return;
  }

  int dx = valueMoveX - centerX;
  int dy = valueMoveY - centerY;

  if (abs(dx) < DEADZONE) dx = 0;
  if (abs(dy) < DEADZONE) dy = 0;

  int absdx = abs(dx);
  int absdy = abs(dy);

  char cmd = 'S';

  // If neither axis is significant, stop
  if (absdx < HORIZ_MIN && absdy < VERT_MIN) {
    cmd = 'S';
  } else {
    // Axis dominance with a small tie tolerance:
    // prefer horizontal when absdx is nearly equal to absdy (within DOM_TOLERANCE)
    if (absdx >= absdy - DOM_TOLERANCE && absdx >= HORIZ_MIN) {
      // horizontal dominates (or tie)
      if (dx < LEFT_OFFSET) cmd = 'L';
      else if (dx > RIGHT_OFFSET) cmd = 'R';
      else cmd = 'S';
    } else if (absdy >= absdx - DOM_TOLERANCE && absdy >= VERT_MIN) {
      // vertical dominates
      if (dy < FORWARD_OFFSET) cmd = 'F';
      else if (dy > BACKWARD_OFFSET) cmd = 'B';
      else cmd = 'S';
    } else {
      // fallback: if vertical is small, allow horizontal; else vertical
      if (absdy < Y_ALLOW_FOR_TURN) {
        if (dx < LEFT_OFFSET) cmd = 'L';
        else if (dx > RIGHT_OFFSET) cmd = 'R';
        else cmd = 'S';
      } else {
        if (dy < FORWARD_OFFSET) cmd = 'F';
        else if (dy > BACKWARD_OFFSET) cmd = 'B';
        else cmd = 'S';
      }
    }
  }

  // debug output
  // Serial.print("x="); Serial.print(valueMoveX);
  // Serial.print(" y="); Serial.print(valueMoveY);
  // Serial.print(" dx="); Serial.print(dx);
  // Serial.print(" dy="); Serial.print(dy);
  // Serial.print(" absdx="); Serial.print(absdx);
  // Serial.print(" absdy="); Serial.print(absdy);
  // Serial.print(" -> "); Serial.println(cmd);

  sendCmd(cmd);
  delay(80);
}
