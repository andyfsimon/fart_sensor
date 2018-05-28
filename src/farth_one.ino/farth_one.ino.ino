#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <SparkFun_Si7021_Breakout_Library.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>
#include <OneButton.h>
#include <ArduinoJson.h>
  
#define BUTTON_PIN 13

// WI-FI Credentials
String ssid = "<SSID>";
String password = "<PASSWORD>";

// MQTT Credentials
String mqtt_server = "<MQTT_SERVER>";
String mqtt_user = "<MQTT_USER>";
String mqtt_password = "<MQTT_PASSWORD>";

// InfluxDB Credentials
String influx_server = "<INFLUX_SERVER>";
String influx_user = "<INFLUX_USER>";
String influx_password = "<INFLUX_PASSWORD>";

// Home Assistant Credentials
String ha_server = "<HA_SERVER>";
String ha_password = "<HA_PASSWORD>";

// Sensor Variables
float bme_humidity = 0;
float bme_temperature = 0;
float bme_pressure = 0;
float bme_airquality = 0;
float bme_altitude = 0;
float si_humidity = 0;
float si_temperature = 0;
float SEALEVELPRESSURE_HPA = 0;

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
Weather si;

// Clients
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
HTTPClient influxdb;
HTTPClient hass;

// Button
OneButton button(BUTTON_PIN, true);


void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println(F("Fart Sensor Study"));

  // Leds
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

  mqtt.setServer(mqtt_server.c_str(), 8883);
  mqtt.setCallback(mqtt_callback);

  button.attachClick(Click);

  getSeaLevelPressure();

}

void loop() {
  
  button.tick();
  if ( WiFi.status() != WL_CONNECTED ) {
    setup_wifi();
  }
  
  if (!mqtt.connected() && WiFi.status() == WL_CONNECTED ) {
    mqtt_reconnect();
  }
  mqtt.loop();
  
  if ( (last_sealevel + 60000) < millis() ) {
    getSeaLevelPressure();
    last_sealevel = millis();
  }

  if ( (last_sensors + 5000) < millis() ) {
    getSiReadings();
    getBMEReadings();

    // Dump data to influxdb
    Serial.print("Dumping data...");
    influxdb_dump();
    Serial.println("done.");
    last_sensors = millis();

  }

    
}

void influxdb_dump()
{  
  // Dump data to influxdb
  influx_data = "";
  influxdb.begin("http://" + influx_server + ":8086/write?db=fartsensor");
  influxdb.addHeader("Content-Type", "application/x-www-form-urlencoded");
  influxdb.setAuthorization(influx_user.c_str(), influx_password.c_str());
  influx_data += "humidity,source=bme value=" + String(bme_humidity) + "\n";
  influx_data += "humidity,source=si value=" + String(si_humidity) + "\n";
  influx_data += "temperature,source=bme value=" + String(bme_temperature) + "\n";
  influx_data += "temperature,source=si value=" + String(si_temperature) + "\n";
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


void Click() {
  Serial.println("Takin' a dump");

  // Light up blue led for 2 secs to report acknowledgement
  digitalWrite(0, LOW);
  // Set topic for "taking a dump" (on)
  dumpin = 1;
  // Force reading
  getSiReadings();
  getBMEReadings();
  delay (1000);
  // Dump data
  influxdb_dump();
  //mqtt.publish("env/bathroom/dumpin",  String(dumpin).c_str(), true);

  delay (5000);
  digitalWrite(0, HIGH);
  // Set topic for "taking a dump" (off)
  dumpin = 0;
  influxdb_dump();
  //mqtt.publish("env/bathroom/dumpin",  String(dumpin).c_str(), true);

}

void getSeaLevelPressure()
{
  hass.begin("http://" + ha_server + ":8123/api/states/sensor.br_pressure?api_password=" + ha_password );
  hass.addHeader("Content-Type", "application/json");
  httpCode = -1;
  while(httpCode == -1){
    httpCode = hass.GET();
  }
  payload = hass.getString();
  hass.end();
  
  const size_t bufferSize = JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(7) + 566 ;
  DynamicJsonBuffer jsonBuffer(bufferSize);
  JsonObject& root = jsonBuffer.parseObject(payload);

  if ( root["state"] != "" ) {
    SEALEVELPRESSURE_HPA = float(root["state"]);
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

void getSiReadings()
{
  // Measure Relative Humidity from the Si7021
  si_humidity = si.getRH();
  // Measure Temperature from the Si7021
  si_temperature = si.getTemp();
}

void setup_wifi() {

    delay(10);
    // We start by connecting to the WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid.c_str(), password.c_str());

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

void mqtt_reconnect() {
  if (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");

    String clientId = "MQTT-FartSensor";
    // Attempt to connect
    if (mqtt.connect(clientId.c_str(), mqtt_user.c_str(), mqtt_password.c_str())) {
      Serial.println("connected");
      //mqtt.subscribe("lights/kitchenled/warmstatus");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again on next loop");
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

  if ( strcmp(topic,"lights/kitchenled/warmstatus") == 0 ) {
    payload[length] = '\0';
  //  warmledState = String((char*)payload).toInt();
  }
  if ( strcmp(topic,"lights/kitchenled/coldstatus") == 0 ) {
    payload[length] = '\0';
  //  coldledState = String((char*)payload).toInt();
  } 

}
