#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

const char *ssid = "RC_Car_AP";
const char *password = "12345678";

#define VRY_MOVE_PIN 34 // Y-axis analog pin
#define VRX_MOVE_PIN 35 // X-axis analog pin
#define SW_PIN 12

#define FORWARD_THRESHOLD 2800
#define BACKWARD_THRESHOLD 3100

#define LEFT_THRESHOLD 2800
#define RIGHT_THRESHOLD 3100

WiFiUDP udp;
const char *carIp = "192.168.4.1";
const uint16_t carPort = 4210;

char lastSent = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(SW_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to AP ");
  Serial.print(ssid);
  Serial.print(" ...");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to AP. Check AP or SSID/password.");
  }

  udp.begin(WiFi.localIP(), 0);
}

void sendCmd(char cmd, bool force=false) {
  if (!force && cmd == lastSent) return;
  lastSent = cmd;
  Serial.print("Sending: ");
  Serial.println(cmd);

  udp.beginPacket(carIp, carPort);
  udp.write((uint8_t *)&cmd, 1);
  udp.endPacket();
}

void loop() {
  int valueMoveY = analogRead(VRY_MOVE_PIN);
  int valueMoveX = analogRead(VRX_MOVE_PIN);
  int swState = digitalRead(SW_PIN);

  if (swState == LOW) {
    sendCmd('S', true);
    delay(50);
    return;
  }

  char cmd = 'S';

  if (valueMoveY < FORWARD_THRESHOLD) {
    cmd = 'F';
  } else if (valueMoveY > BACKWARD_THRESHOLD) {
    cmd = 'B';
  }

  if (valueMoveX < LEFT_THRESHOLD) {
    if (cmd == 'S') cmd = 'L';
  } else if (valueMoveX > RIGHT_THRESHOLD) {
    if (cmd == 'S') cmd = 'R';
  }

  sendCmd(cmd);

  delay(150);
}
