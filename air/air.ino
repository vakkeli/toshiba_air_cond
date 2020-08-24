//dependencies
//https://github.com/plerup/espsoftwareserial


#include <ESP8266WiFi.h>
//#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <WebSocketsServer.h> //Links2004 ///arduino websockets gil maimon
#include <ArduinoJson.h>  //by Benoit Blanchon
#include "toshiba_serial.hpp"
#include "MySimpleTimer.hpp"

const char *w_ssid = "x";
const char *w_passwd = "x";

IPAddress ip(192, 168, 2, 200 );
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);

air_status_t air_status;
MySimpleTimer timerAC;

//ESP8266WiFiMulti wifiMulti;       // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

ESP8266WebServer server(80);       // create a web server on port 80
WebSocketsServer webSocket(81);    // create a websocket server on port 81

File fsUploadFile;                                    // a File variable to temporarily store the received file

const char *ssid = "aire"; // The name of the Wi-Fi network that will be created
const char *password = "aire";   // The password required to connect to it, leave blank for an open network

const char *OTAName = "ESP8266";           // A name and a password for the OTA service
const char *OTAPassword = "esp8266";

const char* mdnsName = "air"; // Domain name for the mDNS responder

#include "DHT.h"
const int DHTPin = D2;
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPin, DHTTYPE);
MySimpleTimer timerDHT;
int DHTinterval = 30;//120;  //every 5 minutes
#define MAX_DHT_DATA 144//288 // for one day
float dht_h[MAX_DHT_DATA];
float dht_t[MAX_DHT_DATA];
float ac_sensor[MAX_DHT_DATA];
float ac_target[MAX_DHT_DATA];

int dht_idx = 0;

MySimpleTimer timerStatus;
MySimpleTimer timerReadSerial;

/*__________________________________________________________SETUP__________________________________________________________*/

void setup() {


  Serial.begin(115200);        // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println("\r\n");

  startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection

  startOTA();                  // Start the OTA service

  startSPIFFS();               // Start the SPIFFS and list all contents

  startWebSocket();            // Start a WebSocket server

  startMDNS();                 // Start the mDNS responder

  startServer();               // Start a HTTP server with a file read handler and an upload handler

  init_air_serial(&air_status); // Start air conditioning structure and software serial

  startReadSerial();
  startStatus();
  startDHT();


}

void startDHT() {
  timerDHT.setUnit(1000);
  timerDHT.setInterval(60);//DHTinterval);
  timerDHT.repeat();
  timerDHT.start();
  dht.begin();
  dht_h[0] = dht.readHumidity();
  dht_t[0] = dht.readTemperature();
  ac_sensor[0] = air_status.sensor_temp;
  ac_target[0] = air_status.target_temp;
}

void startStatus() {
  timerStatus.setUnit(1000);
  timerStatus.setInterval(10);
  timerStatus.repeat();
  timerStatus.start();
}

void startReadSerial() {
  timerReadSerial.setUnit(1000);
  timerReadSerial.setInterval(1);
  timerReadSerial.repeat();
  timerReadSerial.start();
}

void handleTimer() {
  if (timerAC.isTime()) {
    if (air_status.timer_mode_req == TIMER_POWER_OFF) {
      //set power off
      Serial.println("TIMER - POWER OFF");
      air_set_power_off(&air_status);
    } else {
      //set power on
      Serial.println("TIMER - POWER ON");
      air_set_power_on(&air_status);
    }
  }

}

void handleDHT() {
  if (timerDHT.isTime()) {
    Serial.println("Reading DHT");
    // Reading temperature or humidity takes about 250 milliseconds!
    dht_h[dht_idx] = dht.readHumidity();
    dht_t[dht_idx] = dht.readTemperature();

    ac_target[dht_idx] = air_status.target_temp;
    ac_sensor[dht_idx] = air_status.sensor_temp;
    dht_idx = (dht_idx + 1) % MAX_DHT_DATA;
    
  }
}

void handleStatus() {
  if (timerStatus.isTime()) {
    air_print_status(&air_status);
  }
}

void handleReadSerial() {
  if (timerReadSerial.isTime()) {
    air_parse_serial(&air_status);
  }
}

/*__________________________________________________________LOOP__________________________________________________________*/

void loop() {

  handleTimer();
  handleDHT();
  handleStatus();
  handleReadSerial();

  webSocket.loop();                           // constantly check for websocket events
  server.handleClient();                      // run the server
  ArduinoOTA.handle();                        // listen for OTA events
}

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/

void startWiFi() { //fixed IP
  WiFi.mode(WIFI_STA);
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(w_ssid, w_passwd);
  Serial.print("Connected to:\t");
  Serial.println(w_ssid);

  // wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(200);
    Serial.print('.');
  }

  // show IP
  Serial.println("Connection stablished.");
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
}

/*
  void startWiFi_ap() { // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  WiFi.softAP(ssid, password);             // Start the access point
  Serial.print("Access Point \"");
  Serial.print(ssid);
  Serial.println("\" started\r\n");

  wifiMulti.addAP(w_ssid, w_passwd);   // add Wi-Fi networks you want to connect to
  //wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  //wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");

  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1) {  // Wait for the Wi-Fi to connect
    delay(250);
    Serial.print('.');
  }
  Serial.println("\r\n");
  if (WiFi.softAPgetStationNum() == 0) {     // If the ESP is connected to an AP
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());             // Tell us what network we're connected to
    Serial.print("IP address:\t");
    Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
  } else {                                   // If a station is connected to the ESP SoftAP
    Serial.print("Station connected to ESP8266 AP");
  }
  Serial.println("\r\n");
  }
*/

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
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
  Serial.println("OTA ready\r\n");
}

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}

void startMDNS() { // Start the mDNS responder
  MDNS.begin(mdnsName);                        // start the multicast domain name server
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  server.on("/edit.html",  HTTP_POST, []() {  // If a POST request is sent to the /edit.html address,
    server.send(200, "text/plain", "");
  }, handleFileUpload);                       // go to 'handleFileUpload'

  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
  // and check if the file exists

  server.begin();                             // start the HTTP server
  Serial.println("HTTP server started.");
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {                         // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz";                  // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))                     // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

/*void json2air(String request, air_status_t *air)
  {
  StaticJsonDocument<200> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, request);
  if (error) {
    return;
  }
  }*/

String air_to_json(air_status_t *air)
{
  String response;
  StaticJsonDocument<400> jsonDoc;

  jsonDoc["id"] = "status";
  jsonDoc["save"] = air->save;
  jsonDoc["heat"] = air->heat;
  jsonDoc["cold"] = air->cold;
  jsonDoc["temp"] = air->target_temp;
  jsonDoc["sensor_temp"] = air->sensor_temp;
  jsonDoc["fan"] = air->fan_str;
  jsonDoc["mode"] = air->mode_str;
  jsonDoc["power"] = air->power;
  jsonDoc["timer_mode"] = air->timer_mode_req;
  jsonDoc["timer_enabled"] = timerAC.isEnabled();
  jsonDoc["timer_pending"] = timerAC.pendingTime();
  jsonDoc["timer_time"] = timerAC.getInterval();
  jsonDoc["dht_temp"] = dht_t[(dht_idx-1)%MAX_DHT_DATA];
  jsonDoc["dht_hum"] = dht_h[(dht_idx-1)%MAX_DHT_DATA];
  jsonDoc["decode_errors"] = air->decode_errors;

  int i;
  String str;
  for (i = 0; (i < MAX_CMD_BUFFER) && (i < (air->last_cmd[3] + 5)) ; i++)
    str += ((air->last_cmd[i] < 0x10) ? "0" : "")  + String(air->last_cmd[i], HEX) + " ";
  jsonDoc["last_cmd"] = str;

  str = "";
  //for (i = air->curr_r_idx; (i < MAX_RX_BUFFER) && (i< air->curr_w_idx); i++) round buffer
  for (i = 0; (i < MAX_RX_BUFFER) && (i < air->curr_w_idx); i++) //not round buffer
    //for (i = air->curr_r_idx;  i!= air->curr_w_idx; i=(i+1)%MAX_RX_BUFFER)
    str += ((air->rx_data[i] < 0x10) ? "0" : "")  + String(air->rx_data[i], HEX) + " ";
  jsonDoc["rx_data"] = str;

  serializeJson(jsonDoc, response);

  return response;
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("WS [%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("WS [%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      Serial.printf("WS [%u] Received: %s\n", num, payload);

      //decode json
      StaticJsonDocument<200> jsonDoc;
      DeserializationError error = deserializeJson(jsonDoc, payload);
      if (error) {
        return;
      }

      if (jsonDoc["id"] == "power") {            // power
        if (jsonDoc["value"] == "on") {
          air_set_power_on(&air_status);
        } else if (jsonDoc["value"] == "off") {
          air_set_power_off(&air_status);
        }

      } else if (jsonDoc["id"] == "temp") {
        int val = atoi(jsonDoc["temp"]);

        //if (jsonDoc["temp"] == "1") { //
        if (val == 1) {
          val = air_status.target_temp + 1;
          if (val > 30) val = 30;
          if (val < 19) val = 19;
          air_set_temp(&air_status, val);
          //} else if (jsonDoc["temp"] == "0") { //
        } else if (val == 0) {
          val = air_status.target_temp - 1;
          if (val > 30) val = 30;
          if (val < 19) val = 19;
          air_set_temp(&air_status, val);
        } else {
          if (val > 30) val = 30;
          if (val < 19) val = 19;
          air_set_temp(&air_status, val);
        }
      }
      else if (jsonDoc["id"] == "timer") {
        Serial.println("Received timer");
        int val = atoi(jsonDoc["timer_time"]);
        timerAC.setInterval(val);
        timerAC.start();
        air_status.timer_mode_req = jsonDoc["timer_mode"];
        air_status.timer_time_req = val;
      }
      else if (jsonDoc["id"] == "mode") {
        Serial.println("Received mode");
        if (jsonDoc["value"] == "cool") {
          air_set_mode(&air_status, MODE_COOL);
        } else if (jsonDoc["value"] == "dry") {
          air_set_mode(&air_status, MODE_DRY);
        } else if (jsonDoc["value"] == "fan") {
          air_set_mode(&air_status, MODE_FAN);
        }
      }
      else if (jsonDoc["id"] == "fan") {
        Serial.println("Received fan");
        if (jsonDoc["value"] == "low") {
          air_set_fan(&air_status, FAN_LOW);
        } else if (jsonDoc["value"] == "medium") {
          air_set_fan(&air_status, FAN_MEDIUM);
        } else if (jsonDoc["value"] == "high") {
          air_set_fan(&air_status, FAN_HIGH);
        } else if (jsonDoc["value"] == "auto") {
          air_set_fan(&air_status, FAN_AUTO);
        }
      }
      else if (jsonDoc["id"] == "save") {
        Serial.println("Received save");
        if (jsonDoc["value"] == "1") {
          air_set_save_on(&air_status);
        } else if (jsonDoc["value"] == "0") {
          air_set_save_off(&air_status);
        }
      }
      else if (jsonDoc["id"] == "status") {
        Serial.println("Status");

        String message;
        //int i;
        //for (i = 0; i < MAX_CMD_BUFFER && i < (air_status.last_cmd[3] + 5) ; i++)
        //  message += ((air_status.last_cmd[i] < 0x10) ? "0" : "" ) + String(air_status.last_cmd[i], HEX) + " ";
        //Serial.println(message);

        message = air_to_json(&air_status);
        webSocket.broadcastTXT(message);
      }
      else if (jsonDoc["id"] == "timeseries") {
        Serial.println("TimeSeries");
        String message;


        //message = timeseries_to_json(&air_status);

        //StaticJsonDocument<400> docTimeSeries;
        DynamicJsonDocument docTimeSeries(10000);  //3000 hold around 150 vals

        docTimeSeries["id"] = "timeseries";
        //docTimeSeries["name"] = "dht_t";
        docTimeSeries["n"] = MAX_DHT_DATA;

        JsonArray arr = docTimeSeries.createNestedArray("dht_t");
        //arr.add(5);
        int i;
        arr.add(dht_t[dht_idx]);
        for (i = (dht_idx + 1) % MAX_DHT_DATA; i != dht_idx; i = (i + 1) % MAX_DHT_DATA) {
          //for(i=0;i<6;i++){
          //Serial.print("J");Serial.print(i);Serial.print(" ");
          arr.add(dht_t[i]);
        }

        //serializeJson(docTimeSeries, message);
        //webSocket.broadcastTXT(message);

        //delay(50);
          
        //docTimeSeries.clear();
        //docTimeSeries["id"] = "timeseries";
        //docTimeSeries["name"] = "dht_h";
        //docTimeSeries["n"] = MAX_DHT_DATA;
       
        JsonArray arr2 = docTimeSeries.createNestedArray("dht_h");
    
        arr2.add(dht_h[dht_idx]);
        for (i = (dht_idx + 1) % MAX_DHT_DATA; i != dht_idx; i = (i + 1) % MAX_DHT_DATA) {
          //for(i=0;i<6;i++){
          //Serial.print("J");Serial.print(i);Serial.print(" ");
          arr2.add(dht_h[i]);
        }

        JsonArray arr3 = docTimeSeries.createNestedArray("ac_target_t");
    
        arr3.add(ac_target[dht_idx]);
        for (i = (dht_idx + 1) % MAX_DHT_DATA; i != dht_idx; i = (i + 1) % MAX_DHT_DATA) {
          //for(i=0;i<6;i++){
          //Serial.print("J");Serial.print(i);Serial.print(" ");
          arr3.add(ac_target[i]);
        }

        JsonArray arr4 = docTimeSeries.createNestedArray("ac_sensor_t");
    
        arr4.add(ac_sensor[dht_idx]);
        for (i = (dht_idx + 1) % MAX_DHT_DATA; i != dht_idx; i = (i + 1) % MAX_DHT_DATA) {
          //for(i=0;i<6;i++){
          //Serial.print("J");Serial.print(i);Serial.print(" ");
          arr4.add(ac_sensor[i]);
        }
        
        serializeJson(docTimeSeries, message);
        webSocket.broadcastTXT(message);
      }
      break;
  }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  }
}

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}
