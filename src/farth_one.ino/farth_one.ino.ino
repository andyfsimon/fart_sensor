#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include "SparkFun_Si7021_Breakout_Library.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "OneButton.h"
  
// WI-FI Credentials
//const char* ssid = "<SSID>";
//const char* password = "<PASSWORD>";
const char* ssid = "BeaverNet";
const char* password = "supercazzora";

// MQTT Credentials
const char* mqtt_server = "<MQTT_SERVER>";
const char* mqtt_user = "<MQTT_USER>";
const char* mqtt_password = "<MQTT_PASSWORD>";

// InfluxDB Credentials
const char* influx_server = "<MQTT_SERVER>";
const char* influx_user = "<MQTT_USER>";
const char* influx_password = "<MQTT_PASSWORD>";

float bme_humidity = 0;
float bme_temperature = 0;
float bme_pressure = 0;
float bme_airquality = 0;
float bme_altitude = 0;
float si_humidity = 0;
float si_temperature = 0;
float SEALEVELPRESSURE_HPA = 1000.00;

// I2C Sensors
Adafruit_BME680 bme;
Weather si;

// Clients
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Button
OneButton button(13, true);


void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println(F("Fart Sensor Study"));

  pinMode (0, OUTPUT);
  pinMode (2, OUTPUT);
  digitalWrite(0, HIGH);
  digitalWrite(2, HIGH);

  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
    while (1) {
      digitalWrite(0, HIGH);
      delay(1000);
      digitalWrite(0, LOW);
      delay(1000);
    }
  }
  if ( !si.begin()) {
    Serial.println("Could not find a valid Si7021 sensor, check wiring!");
    while (1) {
      digitalWrite(0, LOW);
      delay(200);
      digitalWrite(0, HIGH);
      delay(200);
      digitalWrite(0, LOW);
      delay(200);
      digitalWrite(0, HIGH);
      delay(1000);
    }
  }
  
  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
  
  setup_wifi();

  client.setServer(mqtt_server, 8883);
  client.setCallback(mqtt_callback);

  button.attachClick(Click);

}

void loop() {
  
  button.tick();
  if (!client.connected() && WiFi.status() == WL_CONNECTED ) {
    mqtt_reconnect();
  }
  client.loop();
  
  Serial.println();
  delay(2000);
  getSiReadings();
  printInfo();
 
}

void Click() {
  Serial.println("Takin' a dump");

  // Light up blue led for 2 secs to report acknowledgement
  digitalWrite(0, LOW);
  // Set topic for "taking a dump" (on)
  dumpin = 1;
  client.publish("env/bathroom/dumpin",  String(dumpin).c_str(), true);
  delay (2000);
  digitalWrite(0, HIGH);
  // Set topic for "taking a dump" (off)
  dumpin = 0;
  client.publish("env/bathroom/dumpin",  String(dumpin).c_str(), true);

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

void getSiReadings()
{
  // Measure Relative Humidity from the HTU21D or Si7021
  si_humidity = si.getRH();
  // Measure Temperature from the HTU21D or Si7021
  si_temperature = si.getTemp();
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
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  if ( strcmp(topic,"lights/kitchenled/warmstatus") == 0 ) {
    payload[length] = '\0';
//    warmledState = String((char*)payload).toInt();
  }
  if ( strcmp(topic,"lights/kitchenled/coldstatus") == 0 ) {
    payload[length] = '\0';
//    coldledState = String((char*)payload).toInt();
  } 

}
