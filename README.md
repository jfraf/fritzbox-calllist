# fritzbox-calllist
Ardunino project using an ESP32 and a WaveShare 4.2" ePaper display to retrieve and display the list of incoming/outgoing/missed calls from a FritzBox via TR-064. The list is retrieved periodically (ESP32 wakes from deep sleep) and can be filtered using three hardware buttons.

## Libraries
- Arduino-TR-064-SOAP-Library (https://github.com/Aypac/Arduino-TR-064-SOAP-Library)
- Adam Rudd's TinyXML (https://github.com/adafruit/TinyXML)
- GxEPD2 (https://github.com/ZinggJM/GxEPD2)
- U8g2_for_Adafruit (https://github.com/olikraus/U8g2_for_Adafruit_GFX)

## Wiring
- Three buttons connected to GPIOs 12-14 and +3.3V
- ePaper module with standard GxEPD2 wiring (BUSY = 4, RST = 16, DC = 17, CS = 5 (SS), CLK = 18 (SCK), DIN = 23 (MOSI), GND = GND, VCC = 3V)

## Showcase
This picture is showing the raw assembly. For the final version, I will put the display and the ESP32 in an 6x9cm (2.5x3.5") picture frame and add three buttons to toggle between incoming, outgoing and missed/recorded calls.
![missed_calls_list](https://raw.githubusercontent.com/jfraf/fritzbox-calllist/master/example-missed_calls.jpg "missed calls")
