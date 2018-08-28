#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "CRC16.h"

//===Change values from here===
const char* ssid = "WIFISSID";
const char* password = "PASSWORD";
const char* hostName = "ESPP1Meter";
const char* dsmrIP = "192.168.1.XXX";
const char* dsmrAuthKey = "XXXXXXX";
const int dsmrPort = 88888;
const bool outputOnSerial = true;
//===Change values to here===

// Vars to store meter readings
char dE[32] = ""; //Meter reading Electrics datetime
float mEVLT = 0; //Meter reading Electrics - consumption low tariff
float mEVHT = 0; //Meter reading Electrics - consumption high tariff
float mEOLT = 0; //Meter reading Electrics - return low tariff
float mEOHT = 0; //Meter reading Electrics - return high tariff
float mEAV = 0;  //Meter reading Electrics - Actual consumption
float mEAT = 0;  //Meter reading Electrics - Actual return
float mGAS = 0;  //Meter reading Gas
char dGas[32] = ""; //Meter reading Gas datetime

#define MAXLINELENGTH 128 // longest normal line is 47 char (+3 for \r\n\0)
char telegram[MAXLINELENGTH];

#define SERIAL_RX     D5  // pin for SoftwareSerial RX
SoftwareSerial mySerial(SERIAL_RX, -1, true, MAXLINELENGTH); // (RX, TX. inverted, buffer)

unsigned int currentCRC=0;

void setup() {
  Serial.begin(115200);
  
  Serial.println("Booting");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  mySerial.begin(115200);
  
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostName);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

bool sendToDSMR(char* sValue) {
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  if (!client.connect("192.168.1.200", 8888)) {
    Serial.println("connection failed");
    return false;
  }
  
  client.println("POST /api/v2/datalogger/dsmrreading HTTP/1.1");
  client.println("Host: 192.168.1.200");
  client.println("Connection: close");
  client.print("X-AUTHKEY: ");
  client.println(dsmrAuthKey);
  client.println("Content-Type: application/x-www-form-urlencoded;");
  client.print("Content-Length: ");
  client.println(strlen(sValue));
  client.println();
  client.println(sValue);
  
  //delay(1500);
  //while (client.available()) {
  //  String line = client.readStringUntil('\r');
  //  Serial.println(line);
  //}
  return true;
}

void sendUpdate() {
  char sValue[512];
  sprintf(sValue, "timestamp=%s&electricity_currently_delivered=%.03f&electricity_currently_returned=%.3f&electricity_delivered_1=%.3f&electricity_delivered_2=%.3f&electricity_returned_1=%.3f&electricity_returned_2=%.3f&extra_device_timestamp=%s&extra_device_delivered=%.3f", dE, mEAV, mEAT, mEVLT, mEVHT, mEOLT, mEOHT, dGas, mGAS);
  sendToDSMR(sValue);
}

bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer) {
  //check if the incoming value is valid
  if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer))) {
    return valOld;
  }
  return valNew;
}

double getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return atof(res);
      //return (1000 * atof(res));
    }
  }
  return 0;
}

void formatDate(char* s, char *buf) {
    int year, month, day, hour, minute, seconds, offset;
    char period[1];

    sscanf(s, "(%2d%2d%2d%2d%2d%2d%1s)", &year, &month, &day, &hour, &minute, &seconds, period);
    offset = 1;
    if (String (period) == "S") {
      offset = 2;
    }

    sprintf(buf, "20%02d-%02d-%02dT%02d:%02d:%02d%%2B%02d", year, month, day, hour, minute, seconds, offset);
}

bool decodeTelegram(int len) {
  //need to check for start
  int startChar = FindCharInArrayRev(telegram, '/', len);
  int endChar = FindCharInArrayRev(telegram, '!', len);
  bool validCRCFound = false;
  if(startChar>=0) {
    //start found. Reset CRC calculation
    currentCRC=CRC16(0x0000,(unsigned char *) telegram+startChar, len-startChar);
    if(outputOnSerial)
    {
      for(int cnt=startChar; cnt<len-startChar;cnt++)
        Serial.print(telegram[cnt]);
    }    
  } else if(endChar>=0) {
    //add to crc calc 
    currentCRC=CRC16(currentCRC,(unsigned char*)telegram+endChar, 1);
    char messageCRC[5];
    strncpy(messageCRC, telegram + endChar + 1, 4);
    messageCRC[4]=0; //thanks to HarmOtten (issue 5)
    if(outputOnSerial) {
      for(int cnt=0; cnt<len;cnt++) {
        Serial.print(telegram[cnt]);
      }
    }    
    validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
    if(validCRCFound) {
      Serial.println("\nVALID CRC FOUND!"); 
    } else {
      Serial.println("\n===INVALID CRC FOUND!===");
    }
    currentCRC = 0;
  } else {
    currentCRC=CRC16(currentCRC, (unsigned char*)telegram, len);
    if(outputOnSerial) {
      for(int cnt=0; cnt<len;cnt++) {
        Serial.print(telegram[cnt]);
      }
    }
  }

  //0-0:1.0.0(180814232507S)
  if (strncmp(telegram, "0-0:1.0.0", strlen("0-0:1.0.0")) == 0) {
    char telegramPart[50];
    String(telegram).substring(9).toCharArray(telegramPart, 50);
    formatDate(telegramPart, dE);
  }

  long val =0;
  long val2=0;
  // 1-0:1.8.1(000992.992*kWh)
  // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0) {
    mEVLT = getValue(telegram, len);
  }

  // 1-0:1.8.2(000560.157*kWh)
  // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0) {
    mEVHT = getValue(telegram, len);
  } 

  // 1-0:2.8.1(000348.890*kWh)
  // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0) {
    mEOLT = getValue(telegram, len);
  }

  // 1-0:2.8.2(000859.885*kWh)
  // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
  if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0) {
    mEOHT = getValue(telegram, len);
  }

  // 1-0:1.7.0(00.424*kW) Actueel verbruik
  // 1-0:2.7.0(00.000*kW) Actuele teruglevering
  // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
  if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0) {
    mEAV = getValue(telegram, len);
  }
  if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0){
    mEAT = getValue(telegram, len);
  }

  // 0-1:24.2.1(150531200000S)(00811.923*m3)
  // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
  if (strncmp(telegram, "0-1:24.2.1", strlen("0-1:24.2.1")) == 0) {
    mGAS = getValue(telegram, len);
    
    char telegramPart[50];
    String(telegram).substring(10).toCharArray(telegramPart, 50);
    formatDate(telegramPart, dGas);
  }

  return validCRCFound;
}

void readTelegram() {
  if (mySerial.available()) {
    memset(telegram, 0, sizeof(telegram));
    while (mySerial.available()) {
      int len = mySerial.readBytesUntil('\n', telegram, MAXLINELENGTH);
      telegram[len] = '\n';
      telegram[len+1] = 0;
      yield();
      if(decodeTelegram(len+1)) {
         sendUpdate();
      }
    } 
  }
}

void loop() {
  readTelegram();
  ArduinoOTA.handle();
}



