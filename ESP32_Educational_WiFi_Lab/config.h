#pragma once

// Settings for the ESP32 Educational Wi-Fi Lab.
// Change the access-point name and password before you use this in a classroom.
// Leave the password empty for an open network with no passphrase.
// A WPA2 password must be 8 to 63 characters.

#ifndef LAB_AP_SSID
#define LAB_AP_SSID "ESP32-EduLab2"
#endif

#ifndef LAB_AP_PASSWORD
#define LAB_AP_PASSWORD ""
#endif

// Channels 1, 6, and 11 do not overlap. 1-11 is valid in every region.
#ifndef LAB_AP_CHANNEL
#define LAB_AP_CHANNEL 6
#endif

#ifndef LAB_AP_MAX_CLIENTS
#define LAB_AP_MAX_CLIENTS 4
#endif

#ifndef LAB_AP_IP_1
#define LAB_AP_IP_1 192
#define LAB_AP_IP_2 168
#define LAB_AP_IP_3 4
#define LAB_AP_IP_4 1
#endif

#ifndef LAB_HOSTNAME
#define LAB_HOSTNAME "esp32-edulab"
#endif

#ifndef LAB_WEB_PORT
#define LAB_WEB_PORT 80
#endif

#ifndef LAB_DNS_PORT
#define LAB_DNS_PORT 53
#endif

// Clear logins saved on this ESP32 by older sketches so the radio does not
// wander off and join a router. This does not change any other device.
#ifndef LAB_CLEAR_STORED_STA
#define LAB_CLEAR_STORED_STA 1
#endif

// Most ESP32 DevKitC boards use GPIO 2 for the user LED, active HIGH.
// Do not move this to GPIO 6-11; those pins are wired to the flash chip.
// Set STATUS_LED_ACTIVE_HIGH to 0 if the LED lights when the pin is LOW.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 2
#endif

#ifndef STATUS_LED_ACTIVE_HIGH
#define STATUS_LED_ACTIVE_HIGH 1
#endif

#ifndef LAB_DEFAULT_NAME
#define LAB_DEFAULT_NAME "ESP32 Lab"
#endif

#ifndef LAB_NAME_MAX
#define LAB_NAME_MAX 32
#endif

#ifndef LAB_NOTE_MAX
#define LAB_NOTE_MAX 96
#endif

#ifndef SCAN_MAX_RESULTS
#define SCAN_MAX_RESULTS 16
#endif

#ifndef SCAN_TIMEOUT_MS
#define SCAN_TIMEOUT_MS 15000
#endif

// Refuse a new scan below this free-heap watermark so the server stays up.
#ifndef LAB_MIN_HEAP_FOR_SCAN
#define LAB_MIN_HEAP_FOR_SCAN 24576
#endif
