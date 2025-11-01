#include <Arduino.h>

#define VRX_PIN 35   // X-axis analog pin
#define VRY_PIN 34   // Y-axis analog pin

#define SAMPLES 5       // how many samples to average per read
#define SAMPLE_DELAY 5  // ms between quick samples

// Deadzone around center to avoid jitter
#define DEADZONE 120    // increase if noisy, decrease for more sensitivity

// Direction enums
enum Dir { DIR_NEUTRAL = 0, DIR_NEG = -1, DIR_POS = 1 };

long centerX = 0;
long centerY = 0;

Dir prevX = DIR_NEUTRAL;
Dir prevY = DIR_NEUTRAL;
String prevCombinedState = ""; // holds last printed combined state

// read averaged analog value
int readAvgPin(uint8_t pin) {
  long sum = 0;
  for (int i = 0; i < SAMPLES; ++i) {
    sum += analogRead(pin);
    delay(SAMPLE_DELAY);
  }
  return int(sum / SAMPLES);
}

void calibrateCenter() {
  // take a few averaged readings to establish neutral center
  long sumX = 0, sumY = 0;
  const int CAL_SAMPLES = 10;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    sumX += readAvgPin(VRX_PIN);
    sumY += readAvgPin(VRY_PIN);
    delay(20);
  }
  centerX = sumX / CAL_SAMPLES;
  centerY = sumY / CAL_SAMPLES;
}

Dir getDirFromValue(int value, long center) {
  if (value < (center - DEADZONE)) return DIR_NEG;
  if (value > (center + DEADZONE)) return DIR_POS;
  return DIR_NEUTRAL;
}

String dirToStringX(Dir d) {
  if (d == DIR_NEG) return "LEFT";
  if (d == DIR_POS) return "RIGHT";
  return "NEUTRAL";
}

String dirToStringY(Dir d) {
  if (d == DIR_NEG) return "FORWARD";   // your mapping: lower = forward
  if (d == DIR_POS) return "BACKWARD";  // higher = backward
  return "NEUTRAL";
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Calibrating joystick center. Leave joystick idle...");
  calibrateCenter();
  Serial.print("CenterX = "); Serial.println(centerX);
  Serial.print("CenterY = "); Serial.println(centerY);
  Serial.println("Ready.");
}

void loop() {
  int x = readAvgPin(VRX_PIN);
  int y = readAvgPin(VRY_PIN);

  Dir curX = getDirFromValue(x, centerX);
  Dir curY = getDirFromValue(y, centerY);

  // Build combined state string (skip NEUTRAL parts)
  String curState = "";
  if (curY != DIR_NEUTRAL) {
    curState += dirToStringY(curY);
  }
  if (curX != DIR_NEUTRAL) {
    if (curState.length() > 0) curState += "+";
    curState += dirToStringX(curX);
  }

  // If combined state changed and not empty, print it once
  if (curState != prevCombinedState) {
    if (curState.length() > 0) {
      Serial.println(curState);
    }
    // Update previous trackers
    prevCombinedState = curState;
  }

  prevX = curX;
  prevY = curY;

  delay(50);
}
