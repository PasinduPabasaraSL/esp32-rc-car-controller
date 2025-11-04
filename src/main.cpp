// vectormap_joystick_with_espnow.ino
#include <Arduino.h>
#include <math.h>
#include <WiFi.h>
#include <esp_now.h>

// pins
#define PIN_X 35
#define PIN_Y 34
#define PIN_SW 12

// ADC
const int ADC_MAX = 4095;

// Filtering
const int AVG_SAMPLES = 5;      // small average
const int MEDIAN_WINDOW = 5;    // median window (odd)
const int SPIKE_DELTA = 1000;   // treat sudden 0/4095 as spike if jump > this

// Hysteresis / thresholds (normalized)
const float ENTER_MAG = 0.35f;  // magnitude to decide movement (0..~1)
const float EXIT_MAG  = 0.25f;  // magnitude to return to stop

// Angular hysteresis in degrees
const float ANGLE_HYST = 15.0f; // degrees tolerance used when near sector boundaries

// buffers
int avgBufX[AVG_SAMPLES], avgBufY[AVG_SAMPLES];
int medBufX[MEDIAN_WINDOW], medBufY[MEDIAN_WINDOW];

// center (calibrated)
int centerX = 0, centerY = 0;

// previous median used for spike rejection
int prevMedX = 0, prevMedY = 0;

// state
char lastCmd = 'S';

// --- ESP-NOW peer MAC (user provided) ---
uint8_t peerMac[] = { 0x20, 0xE7, 0xC8, 0x68, 0xB8, 0x30 };

// helper: median
int medianOfArray(int *arr, int n) {
  int tmp[MEDIAN_WINDOW];
  for (int i = 0; i < n; ++i) tmp[i] = arr[i];
  for (int i = 0; i < n - 1; ++i)
    for (int j = i + 1; j < n; ++j)
      if (tmp[j] < tmp[i]) { int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
  return tmp[n/2];
}

void calibrateCenter(int samples = 40, int delayMs = 12) {
  Serial.println("Calibrating center - keep joystick idle...");
  long sx = 0, sy = 0;
  for (int i = 0; i < samples; ++i) {
    sx += analogRead(PIN_X);
    sy += analogRead(PIN_Y);
    delay(delayMs);
  }
  centerX = sx / samples;
  centerY = sy / samples;
  Serial.print("CenterX="); Serial.print(centerX);
  Serial.print(" CenterY="); Serial.println(centerY);
}

float clampf(float v, float a, float b) { return (v < a) ? a : (v > b) ? b : v; }

// normalize dx to [-1..1] using asymmetric ranges from center to edges
float normalizeDx(int dx, int cx) {
  if (dx >= 0) {
    float r = (float)(ADC_MAX - cx);
    if (r <= 1.0f) return 0.0f;
    return (float)dx / r;
  } else {
    float r = (float)cx;
    if (r <= 1.0f) return 0.0f;
    return (float)dx / r; // negative
  }
}

// normalize dy similarly
float normalizeDy(int dy, int cy) {
  if (dy >= 0) {
    float r = (float)(ADC_MAX - cy);
    if (r <= 1.0f) return 0.0f;
    return (float)dy / r;
  } else {
    float r = (float)cy;
    if (r <= 1.0f) return 0.0f;
    return (float)dy / r;
  }
}

// map angle (radians) to degrees [-180,180)
float rad2deg(float r) { return r * 180.0f / M_PI; }

// determine direction from angle (deg)
char angleToDir(float angleDeg) {
  if (angleDeg > -45.0f && angleDeg <= 45.0f) return 'R';
  if (angleDeg > 45.0f && angleDeg <= 135.0f) return 'F';
  if (angleDeg > 135.0f || angleDeg <= -135.0f) return 'L';
  if (angleDeg > -135.0f && angleDeg <= -45.0f) return 'B';
  return 'S';
}

// --- ESP-NOW helper: print MAC ---
void printMac(const uint8_t *mac) {
  for (int i = 0; i < 6; ++i) {
    if (i) Serial.print(":");
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
  }
}

// --- ESP-NOW send callback (optional debug) ---
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status to ");
  printMac(mac_addr);
  Serial.print(" -> ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "ERR");
}

// Initialize ESP-NOW and add peer (non-fatal if fails)
void initEspNowPeer() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  } else {
    esp_now_register_send_cb(onDataSent);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_is_peer_exist(peerInfo.peer_addr)) esp_now_del_peer(peerInfo.peer_addr);
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.print("ESP-NOW peer added: ");
      printMac(peerMac);
      Serial.println();
    } else {
      Serial.println("Failed to add ESP-NOW peer");
    }
  }
}

// Send single-char command via ESP-NOW (non-blocking)
void espNowSendCmd(char cmd) {
  esp_err_t res = esp_now_send(peerMac, (uint8_t*)&cmd, 1);
  if (res != ESP_OK) {
    // print minimal debug, do not alter logic
    Serial.print("esp_now_send err: ");
    Serial.println(res);
  }
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(PIN_X, ADC_11db);
  analogSetPinAttenuation(PIN_Y, ADC_11db);
  pinMode(PIN_SW, INPUT_PULLUP);

  // init buffers
  for (int i = 0; i < AVG_SAMPLES; ++i) { avgBufX[i] = 0; avgBufY[i] = 0; }
  for (int i = 0; i < MEDIAN_WINDOW; ++i) { medBufX[i] = 0; medBufY[i] = 0; }

  calibrateCenter();
  // initialize buffers with center
  for (int i = 0; i < AVG_SAMPLES; ++i) { avgBufX[i] = centerX; avgBufY[i] = centerY; }
  for (int i = 0; i < MEDIAN_WINDOW; ++i) { medBufX[i] = centerX; medBufY[i] = centerY; }
  prevMedX = centerX; prevMedY = centerY;
  delay(200);
  Serial.println("Ready (vector normalization + hysteresis mode)");

  // Initialize ESP-NOW (added)
  initEspNowPeer();
}

void loop() {
  // read raw samples and compute avg
  static int avgPos = 0;
  long sumx = 0, sumy = 0;
  // average AVG_SAMPLES sequentially (simple rolling average)
  avgBufX[avgPos] = analogRead(PIN_X);
  avgBufY[avgPos] = analogRead(PIN_Y);
  avgPos = (avgPos + 1) % AVG_SAMPLES;
  for (int i = 0; i < AVG_SAMPLES; ++i) { sumx += avgBufX[i]; sumy += avgBufY[i]; }
  int avgx = (int)(sumx / AVG_SAMPLES);
  int avgy = (int)(sumy / AVG_SAMPLES);

  // median smoothing
  for (int i = 0; i < MEDIAN_WINDOW - 1; ++i) { medBufX[i] = medBufX[i + 1]; medBufY[i] = medBufY[i + 1]; }
  medBufX[MEDIAN_WINDOW - 1] = avgx;
  medBufY[MEDIAN_WINDOW - 1] = avgy;
  int medx = medianOfArray(medBufX, MEDIAN_WINDOW);
  int medy = medianOfArray(medBufY, MEDIAN_WINDOW);

  // spike rejection: if median is exactly 0 or 4095 and previous was far away -> treat as spike
  if ((medx == 0 || medx == ADC_MAX) && abs(medx - prevMedX) > SPIKE_DELTA) medx = prevMedX;
  if ((medy == 0 || medy == ADC_MAX) && abs(medy - prevMedY) > SPIKE_DELTA) medy = prevMedY;

  prevMedX = medx; prevMedY = medy;

  // compute dx/dy relative to center
  int dx = medx - centerX;
  int dy = medy - centerY;

  // normalize asymmetrically to [-1..1]
  float dxn = normalizeDx(dx, centerX); // may be -1..1 (float)
  float dyn = normalizeDy(dy, centerY);

  // clamp to [-1,1]
  dxn = clampf(dxn, -1.0f, 1.0f);
  dyn = clampf(dyn, -1.0f, 1.0f);

  // magnitude and angle
  float mag = sqrtf(dxn * dxn + dyn * dyn);
  float angRad = atan2f(-dyn, dxn); // -dyn so up = positive angle
  float angDeg = rad2deg(angRad);

  // button stops override
  if (digitalRead(PIN_SW) == LOW) {
    if (lastCmd != 'S') {
      lastCmd = 'S';
      Serial.println("CMD S (button)");
      // send via ESP-NOW
      espNowSendCmd('S');
    }
    delay(50);
    return;
  }

  // hysteresis decision
  char newCmd = 'S';
  static float lastMag = 0.0f;
  static float lastAng = 0.0f;

  // use enter/exit magnitudes
  if (mag >= ENTER_MAG) {
    // determine direction by angle
    newCmd = angleToDir(angDeg);
    // if near boundary, apply small angular hysteresis: keep last if angle didn't move enough
    if (lastCmd != 'S' && lastCmd != newCmd) {
      float da = fabsf(angDeg - lastAng);
      if (da > 180.0f) da = 360.0f - da;
      if (da < ANGLE_HYST) {
        // keep last direction
        newCmd = lastCmd;
      }
    }
  } else if (mag < EXIT_MAG) {
    newCmd = 'S';
  } else {
    // between exit and enter: keep previous direction if any
    newCmd = lastCmd;
  }

  // publish only when changed
  if (newCmd != lastCmd) {
    lastCmd = newCmd;
    Serial.print("x="); Serial.print(medx);
    Serial.print(" y="); Serial.print(medy);
    Serial.print(" dx="); Serial.print(dx);
    Serial.print(" dy="); Serial.print(dy);
    Serial.print(" dxn="); Serial.print(dxn, 3);
    Serial.print(" dyn="); Serial.print(dyn, 3);
    Serial.print(" mag="); Serial.print(mag, 3);
    Serial.print(" ang="); Serial.print(angDeg, 1);
    Serial.print(" -> "); Serial.println(newCmd);

    // send via ESP-NOW (added)
    espNowSendCmd(newCmd);
  }

  lastMag = mag;
  lastAng = angDeg;

  delay(25); // sampling interval (adjust as needed)
}
