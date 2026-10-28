# ESP32 Educational Wi-Fi Multi-Tool & Captive Portal Lab

Local lab for an **ESP32 DevKitC (ESP-WROOM-32)**. The board starts a WPA2 access point, answers every DNS query from clients on that access point with its own address, and serves a dashboard stored in flash.

No extra parts. Power the board from USB.

## What you will see

- A password-protected Wi-Fi network named `ESP32-EduLab`.
- A phone or laptop that joins it is offered this board as its DNS server, and current phones also receive the DHCP captive-portal address.
- Opening an ordinary `http://` address, or `http://192.168.4.1/`, shows the dashboard.
- The page reports free heap, lowest free heap, uptime, and how many clients are joined.
- **Scan networks** listens for nearby access points and lists SSID, RSSI, channel, and encryption. The board does not join them.
- The form stores a label, a note, and the LED state in RAM on the ESP32 and prints them on the serial port. Rebooting clears them.

HTTPS sites will not open. The lab answers DNS and serves this page. It does not intercept encrypted traffic.

## Libraries

These ship with the ESP32 Arduino core. Do not install Library Manager copies, and do not use the ESP8266 versions.

| Library | Header | Role |
| --- | --- | --- |
| WiFi | `WiFi.h` | Access point and network scan |
| WebServer | `WebServer.h` | Dashboard and JSON routes |
| DNSServer | `DNSServer.h` | Answers every DNS name with the ESP32 address |

## Project files

| File | Purpose |
| --- | --- |
| `ESP32_Educational_WiFi_Lab/ESP32_Educational_WiFi_Lab.ino` | `setup` / `loop` |
| `ESP32_Educational_WiFi_Lab/config.h` | SSID, password, channel, LED pin |
| `ESP32_Educational_WiFi_Lab/firmware.cpp` | Access point, DNS, web server, scan, form |
| `ESP32_Educational_WiFi_Lab/portal_html.h` | Dashboard stored in flash |
| `platformio.ini` | PlatformIO build for the ESP32 Dev Module |

Change the name and password in `config.h` before a classroom session. The password must be 8 to 63 characters.

On boot the sketch clears Wi-Fi logins saved **on this ESP32** by older sketches, so the radio does not join your home router. Set `LAB_CLEAR_STORED_STA` to `0` in `config.h` if you want to keep those saved logins.

## Arduino IDE

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. Open **File → Preferences** and add this Boards Manager URL:

   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

3. Open **Tools → Board → Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**, version **2.0.17 or newer** (3.x is fine).
4. Install the USB driver if the port never appears: CP210x for most DevKitC boards, CH340 for some clones.
5. Open `ESP32_Educational_WiFi_Lab/ESP32_Educational_WiFi_Lab.ino`. Open that folder, not the parent folder. The sketch folder name has to match the `.ino` name.
6. In **Tools** set:
   - Board: **ESP32 Dev Module**
   - Upload Speed: **921600**
   - CPU Frequency: **240MHz**
   - Flash Size: **4MB (32Mb)**
   - Partition Scheme: **Default 4MB with spiffs**
   - Port: the COM port that appears when you plug in the board
7. Click **Upload**.
8. Open **Tools → Serial Monitor** at **115200** baud and press the **EN** button on the board. You should see the SSID, password, and dashboard address.

If upload stops on `Connecting...`, hold the **BOOT** button, tap **EN**, and release **BOOT** when the IDE says it is connecting. If upload fails with a timeout, set Upload Speed to **115200** and try again.

Use a data USB cable. Charge-only cables power the board and never show a COM port.

## PlatformIO

Install [PlatformIO](https://platformio.org/), then open the folder that contains `platformio.ini` (the project root, one level above the sketch).

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

The environment `esp32dev` is the ESP32 Dev Module profile used by a DevKitC / ESP-WROOM-32. If upload fails, set `upload_speed = 115200` in `platformio.ini`.

This project path contains a `#`. If PlatformIO misreads the path, copy the project to a simple folder such as `C:\ESP32_Educational_WiFi_Lab` and open that copy.

## Use the lab

1. On a phone, join **ESP32-EduLab**. Password: **edulab2026** (unless you changed `config.h`).
2. Turn off mobile data for the test so the browser uses the ESP32 Wi-Fi network.
3. Wait for the sign-in or captive-portal popup, or open `http://192.168.4.1/`.
4. A plain HTTP check such as `http://neverssl.com/` should also land on the dashboard. The phone still has to be joined to this access point.
5. Read heap, uptime, and the client count. Watch the serial monitor while a device joins.
6. Press **Scan networks**. The table fills when the scan finishes.
7. Save a label and a note. The preview and the serial line `[config]` show the text stored on the board. **Clear** restores the default label.

The access point and the scan share one radio. Clients can stall for a few seconds during a scan. That is normal on the ESP32.

## If something does not come up

| What you see | What to do |
| --- | --- |
| No COM port | Install the CP210x or CH340 driver. Try another USB cable and port. |
| Serial text is garbage | Set the monitor to 115200. |
| LED blinks forever and serial says the AP could not start | The password in `config.h` is shorter than 8 characters, or the radio did not start. Power-cycle the board. |
| Phone will not join | Re-check the password. Forget the network and join again. Stay on channel 1, 6, or 11. |
| No sign-in popup | Open `http://192.168.4.1/` yourself. Some phones only pop up the portal when mobile data is off. |
| Dashboard loads, HTTPS sites do not | Expected. Use the address above. |
| Scan stays empty | Try again nearer a window. The access point stays up either way. |
| Heap number keeps falling | Run a scan once and wait. The firmware copies results into a fixed table and then calls `WiFi.scanDelete()` so the scan buffer is released. |

## How the firmware stays up

- The web page is a flash constant. The server does not build HTML on the heap.
- Scan results live in a fixed array, capped at 16 networks. The Wi-Fi scan buffer is deleted after the copy.
- The scan is asynchronous. `loop` never blocks on the radio, so the task watchdog is not starved.
- JSON responses use one fixed buffer with escaping for quotes and backslashes.
- Form text is length-limited and control characters are dropped.
- A scan is refused if free heap is under 24 KB.
- If the access point cannot start, the web server is left down and the LED blinks. `delay` in that path still feeds the watchdog.
