#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>

// MySQL
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

// --- Homekit ---
#include "arduino_homekit_server.h"

extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t name;
extern "C" char ACCESSORY_NAME[32];
extern "C" void accessory_init();

// ---------------

#include <Arduino.h>
#include "MHZ19.h"
#define BAUDRATE 9600                                      // Device to MH-Z19 Serial baudrate (should not be changed)

MHZ19 myMHZ19;                                             // Constructor for library
HardwareSerial mySerial(2);

const char* ssid = "";
const char* password = "";

IPAddress sql_server_addr(10,0,0,1); 
char user[] = "";                     
char passwordSQL[] = "";
// Sample query
char INSERT_DATA[] = "INSERT INTO home.esp_co2 (host, location, co2, temp) VALUES ('%s','%s',%s,%s)";
char query[128];

unsigned long previousMillis;
unsigned long interval = 5 * 60 * 1000;      // 5 Minuten

#define LED_BUILTIN 2

// Auto Reboot nach 12h
long rebootTime = 12 * 60 * 60 * 1000; // Stunden * Minuten * Sekunden * Millisek
long bootTime = 0;                     // Zeit bei Start

WiFiClient client;
MySQL_Connection conn((Client *)&client);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

String readCO2() {
  return String(myMHZ19.getCO2());
}

String readTemp(){
  return String(myMHZ19.getTemperature());
}

void setupCO2(){
  mySerial.begin(BAUDRATE);                    // SERIAL_8N1, RX_PIN, TX_PIN); 
  myMHZ19.begin(mySerial);                     // *Serial(Stream) refence must be passed to library begin(). 
  myMHZ19.autoCalibration(false);              // INDOOR, autocali off

  homekit_setup();
}

void setup(){
  // Serial port for debugging purposes
  Serial.begin(9600);
  
  // Turn LED off - BLUE
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  // Setup CO2 Sensor
  setupCO2();
  
  // Initialize SPIFFS
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }

  // Print ESP32 Local IP Address
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html");
  });
  server.on("/co2", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readCO2().c_str());
  });
  server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", readTemp().c_str());
  });

  // Setup interval
  previousMillis = millis();

  // Start server
  server.begin();
}

void sendDataToSQL() {
  // Serial.println("Connecting...");
  if (conn.connect(sql_server_addr, 6033, user, passwordSQL)) {
    delay(1000);
    // Initiate the query class instance
    MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);
    // Save
    String co2 = readCO2();
    String temp = readTemp();
    
    sprintf(query, INSERT_DATA, "ESP32", "Wohnzimmer", co2, temp);
    // Execute the query
    cur_mem->execute(query);
    // Note: since there are no results, we do not need to read any data
    // Deleting the cursor also frees up memory used
    delete cur_mem;
    // Serial.println("Data recorded.");
  }
  else
    Serial.println("Connection failed.");
  conn.close();
}
 
void loop(){
  if (millis() - previousMillis > interval ) {
    previousMillis = millis();
 
    // DO
    sendDataToSQL();
  }

  // Auto Reboot nach 12h
  if (millis() - bootTime >= rebootTime) {
    Serial.println("-- Reboot ESP! --");
    ESP.restart();
  }
}

void homekit_setup() {
  accessory_init();
  // We create one FreeRTOS-task for HomeKit
  // No need to call arduino_homekit_loop
  arduino_homekit_setup(&config);
}
