#include <Arduino.h>
#include <.env.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <math.h>

#define TIME_BEFORE_TIMER 2000
#define TIME_BEFORE_OWM 3000
#define TIME_BEFORE_TEMP 3000
#define TIME_BEFORE_THING 3000
#define TIME_BEFORE_L1 3000
#define TIME_BEFORE_L2 3000

#define STATUS_TIMER 0x01
#define STATUS_OWM 0x02
#define STATUS_TEMP 0x04
#define STATUS_THING 0x08
#define STATUS_L1 0x10
#define STATUS_L2 0x20

#define NTP_INTERVAL 14400000
#define NTP_OFFSET 0

#define T_NEXT(STATUS, INTERVAL)\
  status = STATUS;\
  interval = INTERVAL;\
  lastRequest = millis();\
  return;

uint8_t status = STATUS_TIMER;
uint32_t lastRequest = 0;
uint32_t interval = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", NTP_OFFSET, NTP_INTERVAL);

StaticJsonDocument<400> owmFilter;
String owmServer = "https://api.openweathermap.org/data/2.5/weather?units=metric&lang=nl&lat=" + (String) OWM_LAT + "&lon=" + (String) OWM_LON + "&appid=" + (String) OWM_APIKEY;

StaticJsonDocument<100> tempFilter;
String tempServer = "http://" + (String) TEMP_SENSOR_IP + "/as";

String thingspeakServer = "https://api.thingspeak.com/update";

inline String httpGETRequest(const char* serverName, bool ssl_en) {
  WiFiClientSecure clientSec;
  WiFiClient client;
  HTTPClient http;
  String payload = "{}";
  int httpResponseCode;

  if (ssl_en){
    clientSec.setInsecure();
    clientSec.connect(serverName, 443);
    clientSec.setTimeout(2); 
    http.begin(clientSec, serverName);       
  } else {
    client.connect(serverName, 80);
    client.setTimeout(2);
    http.begin(client, serverName);     
  }

  httpResponseCode = http.GET();
  
  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  return payload;
}

inline int httpPOSTRequest(const char* serverName, String postData){
  WiFiClientSecure clientSec;
  HTTPClient http;
  int httpResponseCode;
  clientSec.setInsecure();
  clientSec.connect(serverName, 443);
  clientSec.setTimeout(2);
  http.begin(clientSec, serverName);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  httpResponseCode = http.POST(postData);
  http.end();
  return httpResponseCode;
}

void setup() {
  Serial.begin(115200);

  delay(5);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PW);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }

  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  timeClient.begin();

  owmFilter["main"]["temp"] = true;
  owmFilter["main"]["pressure"] = true;
  owmFilter["main"]["humidity"] = true;
  owmFilter["wind"]["speed"] = true;
  owmFilter["rain"]["1h"] = true;
  owmFilter["clouds"]["all"] = true;
  tempFilter["avg"] = true;

  T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
}

void loop() {

  static uint16_t lastSample = 0;
  uint16_t newSample;
  static DynamicJsonDocument owmDoc(2048);
  static DynamicJsonDocument tempDoc(2048);
  DeserializationError deserErr;
  String jsonBuf;
  String postData = "";
  String displayServ = "";

  timeClient.update();
  newSample = ((timeClient.getEpochTime()  % 86400L) / INTERVAL_SEC) + 1;

  if (!lastSample){
    lastSample = newSample;
  }

  if (newSample != lastSample){
    if (!lastSample){
      lastSample = newSample;
      return;
    }
    lastSample = newSample;
    Serial.println(timeClient.getFormattedTime());
    T_NEXT(STATUS_OWM, TIME_BEFORE_OWM)
  }

  if (status != STATUS_TIMER 
    && millis() - lastRequest > interval){

    if (WiFi.status() != WL_CONNECTED){
      Serial.println("WiFi Disconnected");
      T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
    }

    if (status == STATUS_OWM){
      jsonBuf = httpGETRequest(owmServer.c_str(), true);
      deserErr = deserializeJson(owmDoc, jsonBuf, DeserializationOption::Filter(owmFilter));
      if (deserErr != DeserializationError::Ok){
        Serial.println("deserialization error owm.");
        Serial.println(jsonBuf);
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      if (owmDoc.isNull()){
        Serial.println("owmDoc is null.");
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      Serial.println("owmBuff: ");
      Serial.println(jsonBuf);
      if ((String) owmDoc["main"]["temp"] == "null"){
        Serial.println("owm air temp is null.");
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      serializeJsonPretty(owmDoc, Serial);
      Serial.println();
      T_NEXT(STATUS_TEMP, TIME_BEFORE_TEMP);
    }

    if (status == STATUS_TEMP){
      jsonBuf = httpGETRequest(tempServer.c_str(), false);
      deserErr = deserializeJson(tempDoc, jsonBuf, DeserializationOption::Filter(tempFilter));
      if (deserErr != DeserializationError::Ok){
        Serial.println("deserialization error temp.");
        Serial.println(jsonBuf);
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      if (tempDoc.isNull()){
        Serial.println("tempDoc is null.");
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      if ((String) tempDoc["avg"] == "null"){
        Serial.println("water temp is null.");
        T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
      }
      serializeJsonPretty(tempDoc, Serial);
      Serial.println();
      T_NEXT(STATUS_THING, TIME_BEFORE_THING);
    }

    if (status == STATUS_THING){
      postData = "api_key=";
      postData += (String) THINGSPEAK_APIKEY;
      postData += "&field1=";
      postData += (String) tempDoc["avg"];
      postData += "&field2=";
      postData += (String) owmDoc["main"]["temp"];
      postData += "&field3=";
      postData += (String) owmDoc["main"]["pressure"];
      postData += "&field4=";
      postData += (String) owmDoc["main"]["humidity"];
      postData += "&field5=";
      postData += (String) owmDoc["wind"]["speed"];
      postData += "&field6=";
      postData += (String) owmDoc["rain"]["1h"];
      postData += "&field7=";
      postData += (String) owmDoc["clouds"]["all"];

      Serial.println(postData);
      Serial.print("POST to thingspeak.com channel: ");
      Serial.println(httpPOSTRequest(thingspeakServer.c_str(), postData));

      // Omit display
      // T_NEXT(STATUS_L1, TIME_BEFORE_L1);
      T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
    }

    if (status == STATUS_L1){
      if (((String) tempDoc["avg"]) == "null"){
        Serial.println("L1: temp null, no request");
      } else {
        displayServ = "http://";
        displayServ += (String) TEMP_DISPLAY_IP;
        displayServ += "/l1/";
        displayServ += (String) ((int) floorf((float) tempDoc["avg"]));
        displayServ += ",";
        displayServ += (String) (((int) (((float) tempDoc["avg"]) * 10)) % 10);

        Serial.println(displayServ);
        Serial.print("Display L1: ");
        Serial.println(httpGETRequest(displayServ.c_str(), false));
      }

      T_NEXT(STATUS_L2, TIME_BEFORE_L2);
    }

    if (status == STATUS_L2){

      if (((String) owmDoc["main"]["temp"]) == "null"){
        Serial.println("L2: temp is null, no request");
      } else {
        displayServ = "http://";
        displayServ += (String) TEMP_DISPLAY_IP;
        displayServ += "/l2/";
        displayServ += (String) ((int) floorf((float) owmDoc["main"]["temp"]));
        displayServ += ",";
        displayServ += (String) (((int) (((float) owmDoc["main"]["temp"]) * 10)) % 10);

        Serial.println(displayServ);
        Serial.print("Display L2: ");
        Serial.println(httpGETRequest(displayServ.c_str(), false));
      }

      T_NEXT(STATUS_TIMER, TIME_BEFORE_TIMER);
    }
  }
}