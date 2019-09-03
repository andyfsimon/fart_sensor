#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "Adafruit_VL53L0X.h"
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// WI-FI Credentials
const char* ssid = "********";
const char* password = "*********";

// InfluxDB Credentials
String influx_server = "192.168.0.70";
String influx_user = "hass";
String influx_password = "neon2145";

// Home Assistant Credentials
String ha_server = "192.168.0.8";
String ha_token = "Bearer eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiIyOGRhY2QyMTAzMzY0MzZkYWY4N2YzNTQ1NmJjYzhiZSIsImlhdCI6MTU0ODQyNTAyMiwiZXhwIjoxODYzNzg1MDIyfQ.l_Qo5DUh6msDaPEhZTCwdoYS6tCEFfdEZAAlKzfceHI";

// Sensor Variables
float bme_humidity = 0;
float bme_temperature = 0;
float bme_pressure = 0;
float bme_airquality = 0;
float bme_altitude = 0;
float SEALEVELPRESSURE_HPA = 0;

float centimetri = 0.00;

int dumpin = 0;
int reconnects = 0;

// HTTP Variables
String influx_data = "";
String payload = "";
int httpCode = -1;

// TIMER Variables
unsigned int last_sealevel = millis();
unsigned int last_sensors = millis();

// I2C Sensors
Adafruit_BME680 bme;
Adafruit_VL53L0X distanceSensor = Adafruit_VL53L0X();

unsigned int comincioacontare = 0;
bool stavofuori = true;
bool swappolaluce = false;
String laitstatus = "OFF";

// Clients
WiFiClientSecure espClient;
//PubSubClient mqtt(espClient);
HTTPClient influxdb;
HTTPClient hass;

void setup(void)
{
  Serial.begin(9600);
  while (!Serial);
  Serial.println(F("Fart Sensor Study"));
  Serial.println("Adafruit VL53L0X test");
  if (!distanceSensor.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }
  
  // Leds
  pinMode (0, OUTPUT);
  pinMode (2, OUTPUT);
  pinMode (13, OUTPUT);
  digitalWrite(0, HIGH);
  digitalWrite(2, HIGH);
  digitalWrite(13, LOW);

  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while (1) {
      digitalWrite(0, HIGH);
      delay(1000);
      digitalWrite(0, LOW);
      delay(1000);
    }
  }
  
  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms

  Serial.println("Got Here 1");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("Got Here 2");

  //mqtt.setServer(mqtt_server.c_str(), 8883);
  //mqtt.setCallback(mqtt_callback);


  getSeaLevelPressure();  

  
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
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
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop(void)
{
  
//  button.tick();
  if ( WiFi.status() != WL_CONNECTED ) {
    Serial.println("Got Here 3");
    setup_wifi();
  }
  
  VL53L0X_RangingMeasurementData_t measure;
 
  Serial.print("Reading a measurement... ");
  distanceSensor.rangingTest(&measure, false); // pass in 'true' to get debug data printout!
 
  if (measure.RangeStatus != 4) {  // phase failures have incorrect data
    Serial.print("Distance (cm): "); Serial.println(measure.RangeMilliMeter / 10.00);
    centimetri = measure.RangeMilliMeter / 10.00;
  } else {
    Serial.print(" out of range ");
    centimetri = 99.99;
    Serial.println(centimetri);
  }
 

  if ( centimetri < 10.0 && stavofuori == true ) {
    // Sono entrato nel campo
    Serial.println("Sono Entrato");
    stavofuori = false;
    comincioacontare = millis();
  } else if ( centimetri < 10.00 && (millis() - comincioacontare) > 200 ) {
    // e' ora di swappare la luce
    swappolaluce = true;
    comincioacontare = 0;
  }

  if ( centimetri > 10.00 && stavofuori == false) {
    Serial.println("Sono Uscito");
    stavofuori = true;
  }

  if ( swappolaluce == true && comincioacontare != 0  ) {
    Serial.println("SWAPPO LA LUCE");
    swappolaluce = false;
    if ( laitstatus == "OFF" ) {
      laitstatus = "ON";
      digitalWrite(13, HIGH);
    } else {
      laitstatus = "OFF";
      digitalWrite(13, LOW);
    }
  }

  Serial.println();
  if ( (last_sealevel + 60000) < millis() ) {
    getSeaLevelPressure();
    last_sealevel = millis();
  }

  delay (100);
  
  if ( (last_sensors + 50000) < millis() ) {
    getBMEReadings();

    // Dump data to influxdb
    Serial.print("Dumping data...");
    influxdb_dump();
    Serial.println("done.");
    last_sensors = millis();

  }
  ArduinoOTA.handle();

}

void influxdb_dump()
{  
  // Dump data to influxdb
  influx_data = "";
  influxdb.begin("http://" + influx_server + ":8086/write?db=fartsensor");
  influxdb.addHeader("Content-Type", "application/x-www-form-urlencoded");
  influxdb.setAuthorization(influx_user.c_str(), influx_password.c_str());
  influx_data += "humidity,source=bme value=" + String(bme_humidity) + "\n";
  influx_data += "temperature,source=bme value=" + String(bme_temperature) + "\n";
  influx_data += "airquality,source=bme value=" + String(bme_airquality) + "\n";
  influx_data += "pressure,source=bme value=" + String(bme_pressure) + "\n";
  influx_data += "altitude,source=bme value=" + String(bme_altitude) + "\n";
  influx_data += "dumpin,source=manual value=" + String(dumpin) + "\n";
  influx_data += "reconnects,source=manual value=" + String(reconnects) + "\n";

  httpCode = -1;
  digitalWrite(0, LOW);
  while(httpCode == -1){
    httpCode = influxdb.POST(influx_data);
    influxdb.writeToStream(&Serial);
  }
  delay(10);
  digitalWrite(0, HIGH);

  influxdb.end();
}

void getSeaLevelPressure()
{
  hass.begin("http://" + ha_server + ":8123/api/states/sensor.br_pressure" );
  hass.addHeader("Content-Type", "application/json");
  hass.addHeader("Authorization", ha_token);
  httpCode = -1;
  while(httpCode == -1){
    httpCode = hass.GET();
  }
  payload = hass.getString();
  hass.end();
  //Serial.print("httpcode:");
  //Serial.println(httpCode);
  //Serial.println(payload);
  
  const size_t bufferSize = JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(7) + 566 ; 
  DynamicJsonDocument jsonBuffer(bufferSize);
  deserializeJson(jsonBuffer, payload);

  if ( jsonBuffer["state"] != "" ) {
    SEALEVELPRESSURE_HPA = float(jsonBuffer["state"]);
    Serial.println(SEALEVELPRESSURE_HPA);

  } else {
    Serial.println("Error reading");
  }

}

void getBMEReadings()
{
  if (! bme.performReading()) {
  Serial.println("Failed to perform reading :(");
  return;
  }
  
  bme_temperature = bme.temperature;
  bme_pressure = (bme.pressure / 100.0);
  bme_humidity = bme.humidity;
  bme_airquality = (bme.gas_resistance / 1000.0);
  bme_altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
}

void setup_wifi() {

    delay(10);
    // We start by connecting to the WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println();
      Serial.printf("Connection status: %d\n", WiFi.status());


      
      Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    reconnects++;
    influxdb_dump();
}