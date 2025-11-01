#include <Arduino.h>

#define VRY_MOVE_PIN 34   // Y-axis analog pin
#define VRX_MOVE_PIN 25   // X-axis analog pin

#define FORWARD_THRESHOLD  2800
#define BACKWARD_THRESHOLD 3100

#define LEFT_THRESHOLD  2800
#define RIGHT_THRESHOLD 3100

int valueMoveY = 0;
int valueMoveX = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  valueMoveY = analogRead(VRY_MOVE_PIN);
  valueMoveX = analogRead(VRX_MOVE_PIN);

  if (valueMoveY < FORWARD_THRESHOLD) {
    Serial.println("FORWARD");
  }
  else if (valueMoveY > BACKWARD_THRESHOLD) {
    Serial.println("BACKWARD");
  }
  
  if (valueMoveX < LEFT_THRESHOLD) {
    Serial.println("LEFT");
  }
  else if (valueMoveX > RIGHT_THRESHOLD) {
    Serial.println("RIGHT");
  }

  delay(200);
}