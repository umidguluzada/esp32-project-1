/*
  ESP32 Educational Wi-Fi Multi-Tool & Captive Portal Lab
  Board: ESP32 DevKitC (ESP-WROOM-32)

  config.h        access-point name, password, channel, LED pin
  firmware.cpp    access point, DNS, web server, scan, form storage
  portal_html.h   dashboard stored in flash

  Built-in libraries only: WiFi, WebServer, DNSServer.
  They ship with the ESP32 Arduino core. Do not install the ESP8266 copies.
*/

#include "firmware.h"

void setup() {
  labSetup();
}

void loop() {
  labLoop();
}
