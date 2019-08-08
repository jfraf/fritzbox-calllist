/*
 * fritzbox-calllist.ino: retrieves the received/outgoing/missed calls list from a FritzBox via TR-064 
 * and displays these lists on a 4.2" WaveShare ePaper display.
 * Author: jfraf
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <tr064.h>
#include <TinyXML.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

// user definable variables
const char* wifi_ssid = "#######"; // Wifi network name (SSID)
const char* wifi_password = "#######"; // Wifi network password
const char* IP = "192.168.178.1"; // IP address of your router. Default is "192.168.179.1" for most FRITZ!Boxes
const int PORT = 49000; // Port of the API of your router. This should be 49000 for TR-064 devices.
const char* fuser = "#######"; // The username if you created an account, "admin" otherwise
const char* fpass = "#######"; // The password for the aforementioned account.
const int ncalls = 200; // number of calls to be retrieved and processed, be cautious with too large numbers, these may cause the ESP32 to crash
const int abport = 40; // first phone port of answering machine (40 by default when using FritzBox's internal answering machine)
const String lac = "0821"; // (local) area code to be masked from phone numbers
const unsigned long updateinterval = (1L * 60L) * 1000000L; // Delay between updates, in microseconds

// helper variables and objects
enum alignment {LEFT, RIGHT, CENTER}; // for rendering text strings
enum listmode {ALL, INCOMING, OUTGOING, ACCEPTED, MISSED, RECORDED, MISSED_RECORDED}; // calllist filter options
#define BUTTON_PIN_BITMASK 0x7000 // pusbutton connected to GPIOs 12-14, 111000000000000 in binary
String calls[ncalls][6]; // array to store parsed values from XML calllist
String lastcall = "";
RTC_DATA_ATTR int lastcallid = 0;
int callidx = -1;
uint8_t xmlbuffer[150];
listmode showme = MISSED_RECORDED; // default calllist filter 
RTC_DATA_ATTR listmode lastshowme;

// icons 9x9 px
const unsigned char icon_ab [] PROGMEM = {
  0xff, 0x80, 0x00, 0x00, 0x3e, 0x00, 0x5d, 0x00, 0x6b, 0x00, 0x55, 0x00, 0x3e, 0x00, 0x00, 0x00,
  0xff, 0x80
};
const unsigned char icon_out [] PROGMEM = {
  0xf7, 0x80, 0xe3, 0x80, 0xc1, 0x80, 0x80, 0x80, 0xe3, 0x80, 0xe3, 0x80, 0xe3, 0x80, 0xff, 0x80,
  0x80, 0x80
};
const unsigned char icon_in [] PROGMEM = {
  0xe3, 0x80, 0xe3, 0x80, 0xe3, 0x80, 0x80, 0x80, 0xc1, 0x80, 0xe3, 0x80, 0xf7, 0x80, 0xff, 0x80,
  0x80, 0x80
};
const unsigned char icon_miss [] PROGMEM = {
  0xff, 0x80, 0xb8, 0x00, 0x1c, 0x00, 0x88, 0x00, 0xc1, 0x00, 0xe3, 0x80, 0xf7, 0x80, 0xff, 0x80,
  0x80, 0x80
};
// icons 100x87px
const unsigned char icon_warning [] PROGMEM = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xfe, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 
  0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x01, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xf0, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xf0, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x3f, 
  0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x3f, 0xff, 0xff, 0xff, 
  0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0x80, 0x20, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0x80, 0x70, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf0, 
  0x07, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf8, 0x07, 0xff, 0xff, 
  0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x01, 0xf8, 0x03, 0xff, 0xff, 0xff, 0xff, 0xf0, 
  0xff, 0xff, 0xff, 0xff, 0xfc, 0x01, 0xfc, 0x03, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 
  0xff, 0xfc, 0x03, 0xfc, 0x01, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x03, 
  0xfe, 0x01, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x07, 0xff, 0x00, 0xff, 
  0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0f, 0xff, 0x00, 0x7f, 0xff, 0xff, 0xff, 
  0xf0, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 
  0xff, 0xff, 0xe0, 0x1f, 0xff, 0x80, 0x3f, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xc0, 
  0x1f, 0xff, 0xc0, 0x3f, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x3f, 0xff, 0xe0, 
  0x1f, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x80, 0x7f, 0x9f, 0xe0, 0x1f, 0xff, 0xff, 
  0xff, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x80, 0x7e, 0x07, 0xf0, 0x0f, 0xff, 0xff, 0xff, 0xf0, 0xff, 
  0xff, 0xff, 0xff, 0x00, 0xfe, 0x03, 0xf0, 0x07, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xfe, 
  0x00, 0xfc, 0x03, 0xf8, 0x07, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xfe, 0x01, 0xfc, 0x01, 
  0xf8, 0x03, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xfc, 0x01, 0xfc, 0x01, 0xfc, 0x03, 0xff, 
  0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xfc, 0x03, 0xfc, 0x01, 0xfe, 0x01, 0xff, 0xff, 0xff, 0xf0, 
  0xff, 0xff, 0xff, 0xf8, 0x07, 0xfc, 0x01, 0xfe, 0x00, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 
  0xf0, 0x07, 0xfc, 0x01, 0xff, 0x00, 0xff, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xf0, 0x0f, 0xfc, 
  0x01, 0xff, 0x00, 0x7f, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xe0, 0x0f, 0xfc, 0x01, 0xff, 0x80, 
  0x7f, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0xe0, 0x1f, 0xfc, 0x03, 0xff, 0xc0, 0x3f, 0xff, 0xff, 
  0xf0, 0xff, 0xff, 0xff, 0xc0, 0x1f, 0xfe, 0x03, 0xff, 0xc0, 0x3f, 0xff, 0xff, 0xf0, 0xff, 0xff, 
  0xff, 0xc0, 0x3f, 0xfe, 0x03, 0xff, 0xe0, 0x1f, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0x80, 0x7f, 
  0xfe, 0x03, 0xff, 0xe0, 0x0f, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0x00, 0x7f, 0xfe, 0x03, 0xff, 
  0xf0, 0x0f, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xff, 0x00, 0xff, 0xfe, 0x03, 0xff, 0xf0, 0x07, 0xff, 
  0xff, 0xf0, 0xff, 0xff, 0xfe, 0x00, 0xff, 0xfe, 0x03, 0xff, 0xf8, 0x07, 0xff, 0xff, 0xf0, 0xff, 
  0xff, 0xfe, 0x01, 0xff, 0xff, 0x03, 0xff, 0xfc, 0x03, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xfc, 0x03, 
  0xff, 0xff, 0x07, 0xff, 0xfc, 0x01, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xf8, 0x03, 0xff, 0xff, 0x07, 
  0xff, 0xfe, 0x01, 0xff, 0xff, 0xf0, 0xff, 0xff, 0xf8, 0x07, 0xff, 0xff, 0x07, 0xff, 0xfe, 0x00, 
  0xff, 0xff, 0xf0, 0xff, 0xff, 0xf0, 0x07, 0xff, 0xff, 0x07, 0xff, 0xff, 0x00, 0xff, 0xff, 0xf0, 
  0xff, 0xff, 0xf0, 0x0f, 0xff, 0xff, 0x07, 0xff, 0xff, 0x00, 0x7f, 0xff, 0xf0, 0xff, 0xff, 0xe0, 
  0x0f, 0xff, 0xff, 0x07, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xf0, 0xff, 0xff, 0xe0, 0x1f, 0xff, 0xff, 
  0x07, 0xff, 0xff, 0xc0, 0x3f, 0xff, 0xf0, 0xff, 0xff, 0xc0, 0x3f, 0xff, 0xff, 0x07, 0xff, 0xff, 
  0xc0, 0x1f, 0xff, 0xf0, 0xff, 0xff, 0x80, 0x3f, 0xff, 0xff, 0x07, 0xff, 0xff, 0xe0, 0x1f, 0xff, 
  0xf0, 0xff, 0xff, 0x80, 0x7f, 0xff, 0xff, 0x07, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0xf0, 0xff, 0xff, 
  0x00, 0x7f, 0xff, 0xff, 0x07, 0xff, 0xff, 0xf0, 0x0f, 0xff, 0xf0, 0xff, 0xff, 0x00, 0xff, 0xff, 
  0xff, 0x07, 0xff, 0xff, 0xf8, 0x07, 0xff, 0xf0, 0xff, 0xfe, 0x01, 0xff, 0xff, 0xff, 0x07, 0xff, 
  0xff, 0xf8, 0x03, 0xff, 0xf0, 0xff, 0xfc, 0x01, 0xff, 0xff, 0xff, 0x07, 0xff, 0xff, 0xfc, 0x03, 
  0xff, 0xf0, 0xff, 0xfc, 0x03, 0xff, 0xff, 0xff, 0x07, 0xff, 0xff, 0xfc, 0x01, 0xff, 0xf0, 0xff, 
  0xf8, 0x03, 0xff, 0xff, 0xff, 0x07, 0xff, 0xff, 0xfe, 0x01, 0xff, 0xf0, 0xff, 0xf8, 0x07, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0xff, 0xf0, 0xff, 0xf0, 0x07, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0x00, 0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0x80, 0x7f, 0xf0, 0xff, 0xe0, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x3f, 0xf0, 
  0xff, 0xc0, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x3f, 0xf0, 0xff, 0xc0, 0x3f, 
  0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff, 0xc0, 0x1f, 0xf0, 0xff, 0x80, 0x3f, 0xff, 0xff, 0xfe, 
  0x03, 0xff, 0xff, 0xff, 0xe0, 0x1f, 0xf0, 0xff, 0x80, 0x7f, 0xff, 0xff, 0xfc, 0x01, 0xff, 0xff, 
  0xff, 0xf0, 0x0f, 0xf0, 0xff, 0x00, 0xff, 0xff, 0xff, 0xfc, 0x01, 0xff, 0xff, 0xff, 0xf0, 0x07, 
  0xf0, 0xff, 0x00, 0xff, 0xff, 0xff, 0xf8, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x07, 0xf0, 0xfe, 0x01, 
  0xff, 0xff, 0xff, 0xf8, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x03, 0xf0, 0xfc, 0x01, 0xff, 0xff, 0xff, 
  0xfc, 0x01, 0xff, 0xff, 0xff, 0xfc, 0x03, 0xf0, 0xfc, 0x03, 0xff, 0xff, 0xff, 0xfc, 0x03, 0xff, 
  0xff, 0xff, 0xfc, 0x01, 0xf0, 0xf8, 0x03, 0xff, 0xff, 0xff, 0xfe, 0x07, 0xff, 0xff, 0xff, 0xfe, 
  0x01, 0xf0, 0xf8, 0x07, 0xff, 0xff, 0xff, 0xff, 0x8f, 0xff, 0xff, 0xff, 0xff, 0x00, 0xf0, 0xf0, 
  0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x70, 0xe0, 0x0f, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x70, 0xe0, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xff, 0xff, 0xff, 0x80, 0x30, 0xc0, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0xff, 0xc0, 0x30, 0xc0, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x10, 
  0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30
};

// init objects
WiFiMulti WiFiMulti;
TR064 connection(PORT, IP, fuser, fpass);
TinyXML xml;
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// init display
// wiring scheme to connect waveshare epaper display to esp32:
// BUSY -> 4, RST -> 16, DC -> 17, CS -> SS(5),
// CLK -> SCK(18), DIN -> MOSI(23), GND -> GND, 3.3V -> 3.3V
GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(/*CS=5*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));

// parse xml data from calllist.lua and add to array
void parse_XML_calllist( uint8_t statusflags, char* tagName,  uint16_t tagNameLen,  char* data,  uint16_t dataLen )
{
  if  (statusflags & STATUS_TAG_TEXT) {
    /*
      Serial.print("Tag:");
      Serial.print(tagName);
      Serial.print(" text:");
      Serial.println(data);
    */
    if (strcmp(tagName, "/root/Call/Id") == 0) {
      if (lastcall != data) {
        callidx++;
      }
      calls[callidx][0] = data;
      lastcall = data;
    } else if (strcmp(tagName, "/root/Call/Type") == 0) {
      calls[callidx][1] = String(data);
    } else if (strcmp(tagName, "/root/Call/Caller") == 0) {
      // type 1: incoming, 2: missed
      if (calls[callidx][1] == "1" || calls[callidx][1] == "2") {
        calls[callidx][2] = data;
      }
    } else if (strcmp(tagName, "/root/Call/Called") == 0) {
      // type 3: outgoing
      if (calls[callidx][1] == "3") {
        calls[callidx][2] = data;
      }
    } else if (strcmp(tagName, "/root/Call/Name") == 0) {
      calls[callidx][3] = data;
    } else if (strcmp(tagName, "/root/Call/Date") == 0) {
      calls[callidx][4] = data;
    } else if (strcmp(tagName, "/root/Call/Port") == 0) {
      calls[callidx][5] = data;
    }
  }
}

// show parsed calllist
void printCalllist() {
  for (int z = 0; z <= callidx; z++) {
    for (int y = 0; y < 6; y++) {
      Serial.print(calls[z][y]);
      Serial.print(" ");
    }
    Serial.println("---");
  }
}

// prints aligned text strings
void drawString(int x, int y, String text, alignment align) {
  int8_t h;
  char tmpstr[200];
  text.toCharArray(tmpstr, 200);
  int twidth = u8g2Fonts.getUTF8Width(tmpstr);
  u8g2Fonts.setFontMode(1);  // use u8g2 transparent mode (this is default)
  u8g2Fonts.setFontDirection(0); // left to right (this is default)
  h = u8g2Fonts.getFontAscent();
  switch (align) {
    case CENTER:
      u8g2Fonts.setCursor(int(x - twidth/2), y + h);
      break;
    case RIGHT:
      u8g2Fonts.setCursor(x - twidth, y + h);
      break;
    default:
      u8g2Fonts.setCursor(x, y + h);
      break;
  }
  u8g2Fonts.print(text);
}

// plotting of (filtered) calllist to display (300x400px)
void drawCalllist(listmode m) {
  if (callidx < 0) return;
  display.init();
  u8g2Fonts.begin(display);
  display.setRotation(1);
  display.firstPage();
  do
  {
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    int line = 0;
    int lineheight = 25;
    char tmpstr[50];
    int strw;
    String h = "";
    String tmptype, tmpnumber, tmpcaller, tmptime, tmpport;
    int cols[2] = {0, 110};
    int xmax = display.width();
    int ymax = display.height(); 

    if (m == ALL) h = "Alle Telefonate";
    if (m == INCOMING) h = "Eingehende Anrufe";
    if (m == OUTGOING) h = "Abgehende Gespräche";
    if (m == ACCEPTED) h = "Angenommene Anrufe";
    if (m == MISSED) h = "Verpasste Anrufe";
    if (m == RECORDED) h = "Anrufbeantworter";
    if (m == MISSED_RECORDED) h = "Verpasst / Anrufbeantworter";

    u8g2Fonts.setFont(u8g2_font_helvB14_tf);
    drawString(150, 0, h, CENTER);
    
    for (int s = 0; s < callidx; s++) {
      if ((line+1)*lineheight > ymax) continue;
      
      tmptype = calls[s][1];
      tmpnumber = calls[s][2];
      tmpcaller = calls[s][3];
      tmptime = calls[s][4];
      tmpport = calls[s][5];

      int t = tmptype.toInt(); // 1: incoming, 2: missed, 3: outgoing, 9: active incoming, 10: rejected incoming, 11: active outgoing
      int p = tmpport.toInt(); // FritzBox answering machine has a port of >= 40

      // filter call list
      switch(m) {
        case INCOMING:
          if (t == 1 || t == 2 || t == 9 || t == 10) { } else { continue; }
          break;
        case OUTGOING:
          if (t == 3 || t == 11) { } else { continue; }
          break;
        case ACCEPTED:
          if ((t == 1 || t == 9) && p < abport) { }  else { continue; }
          break;
        case MISSED:
           if (t == 2) { } else { continue; }
          break;
        case RECORDED:
         if (p >= abport) { } else { continue; }
          break;
        case MISSED_RECORDED:
          if (t == 2 || p >= abport) { } else { continue; }
        default:
          break;
      }

      tmptime.remove(6, 2); // omit yy from date
      if (tmpnumber.startsWith(lac)) {
        tmpnumber.remove(0, lac.length()); // remove area code for local numbers
      }
      if (tmpnumber.length() == 0) {
        tmpnumber = "Unbekannt";
        tmpcaller = "";
      }

	  Serial.println(String(tmptype + " " + tmpnumber + " " + tmpcaller + " " + tmptime + " " + tmpport));
	  
      u8g2Fonts.setFont(u8g2_font_helvR14_tf);
      drawString(cols[0], (line + 1)*lineheight, tmptime, LEFT);
      drawString(cols[1] + 14, (line + 1)*lineheight, tmpnumber, LEFT);

      tmpnumber.toCharArray(tmpstr, 50);
      strw = u8g2Fonts.getUTF8Width(tmpstr);
      if (t == 2 || t == 10) display.drawInvertedBitmap(cols[1], (line + 1)*lineheight + 3, icon_miss, 9, 9, GxEPD_BLACK);
      if ((t == 1 || t == 9) && p >= abport) display.drawInvertedBitmap(cols[1], (line + 1)*lineheight + 3, icon_ab, 9, 9, GxEPD_BLACK);
      if ((t == 1 || t == 9) && p < abport) display.drawInvertedBitmap(cols[1], (line + 1)*lineheight + 3, icon_in, 9, 9, GxEPD_BLACK);
      if (t == 3 || t == 11) display.drawInvertedBitmap(cols[1], (line + 1)*lineheight + 3, icon_out, 9, 9, GxEPD_BLACK);

	  // add caller's name in case it is in FritzBox's phonebook
      if (tmpcaller.length() > 0) {
        u8g2Fonts.setFont(u8g2_font_helvR08_tf);
        drawString(cols[1] + strw + 20, (line + 1)*lineheight + 4, tmpcaller, LEFT);
      }

      // dotted lines between entries
      for (int r = 0; r < 400; r += 3) {
        display.drawFastHLine(r, (line + 2)*lineheight - 5, 1, GxEPD_BLACK);
      }
      line++;
    }
  }
  while (display.nextPage());
}

// draw exclamation mark and custom error message to epaper display (300x400px)
void drawWarning(String message) {
  display.init();
  u8g2Fonts.begin(display);
  display.setRotation(1);
  display.firstPage();
  do
  {
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);         // apply Adafruit GFX color
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);         // apply Adafruit GFX color
    display.drawInvertedBitmap(100,100, icon_warning, 100, 87, GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_helvB14_tf);
    drawString(150, 200, message, CENTER);
  }
  while (display.nextPage());
}

// determine GPIO pin used to wake up ESP32 from deep sleep
int get_GPIO_wake_up(){
  int GPIO_reason = (uint32_t)esp_sleep_get_ext1_wakeup_status();
  return (uint16_t)((log(GPIO_reason))/log(2));
}

// set deep sleep parameters and enter deep sleep mode
void deep_sleep() {
  esp_sleep_enable_timer_wakeup(updateinterval);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  gpio_pullup_dis(GPIO_NUM_12);
  gpio_pulldown_en(GPIO_NUM_12);
  gpio_pullup_dis(GPIO_NUM_13);
  gpio_pulldown_en(GPIO_NUM_13);
  gpio_pullup_dis(GPIO_NUM_14);
  gpio_pulldown_en(GPIO_NUM_14);
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  // check which GPIO pin was used to wake up and set display mode accordingly
  switch(get_GPIO_wake_up()) {
    case 12:
      showme = ACCEPTED;
      break;
    case 13:
      showme = OUTGOING;
      break;
    case 14:
      showme = MISSED_RECORDED;
      break;
    default:
      break;
  }
 
  // Connect to wifi
  int connAttempts = 0;
  WiFiMulti.addAP(wifi_ssid, wifi_password);
  while ((WiFiMulti.run() != WL_CONNECTED)) {
    Serial.print(".");
    delay(500);
    connAttempts++;
    if (connAttempts > 60) {
      drawWarning("Keine WLAN-Verbindung");
      deep_sleep();
    }
  }

  Serial.end();

  // initialize TR-064 library
  connection.init();

  // perform SOAP request and store URL of callers list is in req[0][1]
  String params[][2] = {{}};
  String req[][2] = {{"NewCallListURL", ""}};
  connection.action("urn:dslforum-org:service:X_AVM-DE_OnTel:1", "GetCallList", params, 0, req, 1);

  // initialize XML library
  xml.init((uint8_t *)xmlbuffer, sizeof(xmlbuffer), &parse_XML_calllist);

  Serial.begin(115200);
  
  // check if calllist URL could be retrieved
  if (req[0][1].length() == 0) {
    drawWarning("Fehler beim Datenabruf");
    deep_sleep();
  }

  // download and process caller list
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    HTTPClient http;
    Serial.println("[HTTP] begin...\n");
    http.begin(String(req[0][1] + "&max=" + ncalls + "&type=xml"));
    int httpCode = http.GET(); //will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        // get lenght of document (is -1 when server sends no Content-Length header)
        int len = http.getSize();
        // create buffer for read
        uint8_t buff[128] = { 0 };
        // get tcp stream
        WiFiClient * stream = http.getStreamPtr();
        // read all data from server
        while (http.connected() && (len > 0 || len == -1)) {
          // get available data size
          size_t size = stream->available();
          if (size) {
            // read up to 128 byte
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            // Serial.write(buff, c); // for debugging, show content of buffer
            // parse xml stream char by char
            for (int a = 0; a < ((size > sizeof(buff)) ? sizeof(buff) : size); a++) {
              xml.processChar(buff[a]);
            }
            if (len > 0) {
              len -= c;
            }
          }
          delay(1);
        }
        Serial.println();
        Serial.print("[HTTP] connection closed or file end.\n");

        // printCalllist();
        if (lastshowme != showme || lastcallid != calls[0][0].toInt()) {
          Serial.println("Last call's ID differs from RTC memory or display mode changed. Refreshing.");
          Serial.print("Last ID: ");
          Serial.print(lastcallid);
          Serial.print(" Current ID: ");
          Serial.println(calls[0][0].toInt());
          drawCalllist(showme);
          lastcallid = calls[0][0].toInt();
          lastshowme = showme;
        } else {
          Serial.println("Calllist was not updated and/or display mode did not change since last refresh.");
        }
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      drawWarning("Fehler beim Datenabruf");
      deep_sleep();
    }
    http.end();
    Serial.println("Done.");
  }
  
  Serial.println("Entering deep sleep mode.");
  deep_sleep();
}

// nothing to do here
void loop() {
}