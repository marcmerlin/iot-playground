#include <OneWire.h>

// Config includes AdafruitIO
#include "config.h"

// ESP8266 compile options to save RAM: 
// Debug Port: Serial, level Core, v2 lower memory no features, basic SSL ciphers


#include <ESP8266WiFi.h>
WiFiClient  client;

float celsius, fahrenheit;
char wifimac[19];
long rssi;
String status;

// Thinger
// TLS takes too much RAM for the Wifi stack on ESP8266, turn it off
#ifdef THINGER
#define _DISABLE_TLS_
#define _DEBUG_
#include <ThingerESP8266.h>
ThingerESP8266 thing(USERNAME, DEVICE_ID, DEVICE_CREDENTIAL);
#endif

// Thingspeak
#ifdef THINGSPEAK
#include "ThingSpeak.h"
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
#endif

// AdafruitIO
#ifdef ADAIOT
AdafruitIO_Feed *owiretemp = io.feed("1wiretemp" DEVNUMS);
AdafruitIO_Feed *console = io.feed("console");
#endif

// OneWire DS18S20, DS18B20, DS1822 Temperature Example
// http://www.pjrc.com/teensy/td_libs_OneWire.html
// The DallasTemperature library can do all this work for you!
// https://github.com/milesburton/Arduino-Temperature-Control-Library
OneWire  ds(D3);

// As seen in the field, while the ESP8266 is supposed to self-reconnect
// to a flaky network, I have seen it lose its IP and not request a new one:
// #2: 11:22:33:44:55:66 (IP unset) wl-namewrl-2.4 (RSSI: -79). Temp: 68.00
// If IP is unset, trigger a reboot to reset everything clean and recover
void(* resetFunc) (void) = 0; // jump to 0 to cause a sofware reboot

void reset() {
#ifdef ESP8266
    Serial.print("Doing ESP restart Failed");
    ESP.restart();
    Serial.print("ESP Restart Failed");
#endif
    Serial.print("Doing Jump 0 software reset");
    resetFunc();
    Serial.print("Reset Failed");
}

void setup(void) {
    Serial.begin(115200);

    // ---------------------------------------------------------------------
#ifdef ADAIOT
    Serial.print("Connecting to Adafruit IO");
    io.connect();

    // wait for a connection
    while(io.status() < AIO_CONNECTED) {
          Serial.print(".");
          delay(500);
    }
    // we are connected
    Serial.println();
    Serial.println(io.statusText());
#endif
    // ---------------------------------------------------------------------
#ifdef THINGSPEAK
    ThingSpeak.begin(client);
#endif
    // ---------------------------------------------------------------------
#ifdef THINGER
    thing.add_wifi(SECRET_SSID, SECRET_PASS);
    thing["temp" DEVNUMS] >> outputValue(fahrenheit);
    thing["rssi" DEVNUMS] >> outputValue(rssi);
    thing["console"] >> outputValue(status);
#endif
    // ---------------------------------------------------------------------

    // Common
    byte mac[6];
    WiFi.macAddress(mac);
    sprintf(wifimac, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println("Mac: " + String(wifimac));

#ifdef ESP8266
    Serial.println();
    Serial.print( F("Heap: ") ); Serial.println(system_get_free_heap_size());
    Serial.print( F("Boot Vers: ") ); Serial.println(system_get_boot_version());
    Serial.print( F("CPU: ") ); Serial.println(system_get_cpu_freq());
    Serial.print( F("SDK: ") ); Serial.println(system_get_sdk_version());
    Serial.print( F("Chip ID: ") ); Serial.println(system_get_chip_id());
    Serial.print( F("Flash ID: ") ); Serial.println(spi_flash_get_id());
    Serial.print( F("Flash Size: ") ); Serial.println(ESP.getFlashChipRealSize());
    Serial.print( F("Vcc: ") ); Serial.println(ESP.getVcc());
#endif
}

void caltemp(void) {
    byte i;
    byte present = 0;
    byte type_s;
    byte data[12];
    byte addr[8];
    
    if ( !ds.search(addr)) {
          Serial.println("No more addresses.");
          Serial.println();
          ds.reset_search();
          delay(250);
          return;
    }
    
    Serial.print("ROM =");
    for( i = 0; i < 8; i++) {
          Serial.write(' ');
          Serial.print(addr[i], HEX);
    }

    if (OneWire::crc8(addr, 7) != addr[7]) {
                  Serial.println("CRC is not valid!");
                  return;
    }
    Serial.println();
 
    // the first ROM byte indicates which chip
    switch (addr[0]) {
          case 0x10:
                  Serial.println("  Chip = DS18S20");  // or old DS1820
                  type_s = 1;
                  break;
          case 0x28:
                  Serial.println("  Chip = DS18B20");
                  type_s = 0;
                  break;
          case 0x22:
                  Serial.println("  Chip = DS1822");
                  type_s = 0;
                  break;
          default:
                  Serial.println("Device is not a DS18x20 family device.");
                  return;
    } 

    ds.reset();
    ds.select(addr);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
    
    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
    
    present = ds.reset();
    ds.select(addr);    
    ds.write(0xBE);         // Read Scratchpad

    Serial.print("  Data = ");
    Serial.print(present, HEX);
    Serial.print(" ");
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
          data[i] = ds.read();
          Serial.print(data[i], HEX);
          Serial.print(" ");
    }
    Serial.print(" CRC=");
    Serial.print(OneWire::crc8(data, 8), HEX);
    Serial.println();

    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    int16_t raw = (data[1] << 8) | data[0];
    if (type_s) {
          raw = raw << 3; // 9 bit resolution default
          if (data[7] == 0x10) {
                  // "count remain" gives full 12 bit resolution
                  raw = (raw & 0xFFF0) + 12 - data[6];
          }
    } else {
          byte cfg = (data[4] & 0x60);
          // at lower res, the low bits are undefined, so let's zero them
          if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
          else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
          else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
          //// default is 12 bit resolution, 750 ms conversion time
    }
    celsius = (float)raw / 16.0;
    fahrenheit = celsius * 1.8 + 32.0;
    Serial.print("  Temperature = ");
    Serial.print(celsius);
    Serial.print(" Celsius, ");
    Serial.print(fahrenheit);
    Serial.println(" Fahrenheit");
}

void loop(void) {
    caltemp();

    IPAddress ip = WiFi.localIP();
    rssi = WiFi.RSSI();
    Serial.print("IP (used to reboot if connection is lost): ");
    Serial.println(ip.toString());
    if (ip.toString() == String("(IP unset)") ) {
	Serial.println("IP unset, rebooting...");
	reset();
    }    
    // thinger.io supports longer update messages
    status =  "#" DEVNUMS ": " + String(wifimac) + " " + ip.toString() + " " + WIFI_SSID + " (RSSI: " + String(rssi) + "). Temp: " + String(fahrenheit);

    // ---------------------------------------------------------------------
#ifdef THINGER
    // thinger.io handler needs to run often enough or thinger gets upset and
    // won't discover feeds. so we can't wait 30 seconds or longer, as required
    // by the other IOT clouds, and we update this one more often
    thing.handle();
#endif

    // this is the delay loop to ensure we don't push too often to the other clouds
    static int16_t loopdelay = 60;
    if (loopdelay--) return;
    Serial.println(status);
    loopdelay = 60;
    // ---------------------------------------------------------------------
    // thingspeak
#ifdef THINGSPEAK
    // Thingspeak, does not like concurrent writes from multiple devices on the
    // same device account, so we spread them out randomly. This is not needed
    // if you have quota to connect each device to its own account.
    // Also, it accepts messages faster, but they count against your quota, so
    // once a minute on average, is enough.
    loopdelay = random(50, 70);
    int httpCode;
    ThingSpeak.setField(DEVNUM, fahrenheit);
    ThingSpeak.setStatus(status);

    httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (httpCode == 200) {
	Serial.println("ThingSpeak Write Successful.");
    }
    else {
	Serial.println("ThingSpeak Write Failure (401 is returned for concurrent access on the same device). HTTP error code " + String(httpCode));
    }
#endif
    // ---------------------------------------------------------------------
    // Adafruit IO
#ifdef ADAIOT
    // Adafruit IO simply crashes if the message is too long, so we give it shorter messages
    String status1 =  "#" DEVNUMS ": " + String(wifimac) + " " + ip.toString() + " " + WIFI_SSID + " RSSI: " + String(rssi);
    String status2 =  "#" DEVNUMS ": " + String(wifimac) + " Temp: " + String(fahrenheit);
    String statuss;
    static uint8_t status1or2 = 0;
    statuss = (status1or2++ % 2) ? status1 : status2;
    Serial.println(statuss);
    io.run();

    owiretemp->save(fahrenheit);
    console->save(statuss);
#endif
    // ---------------------------------------------------------------------
}
