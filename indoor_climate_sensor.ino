#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

/************ MQTT Information (CHANGE THESE FOR YOUR SETUP) ******************/
#define MQTT_HOST "<your_mqtt_host>"
#define MQTT_PORT 1883
#define MQTT_MAX_PACKET_SIZE 256

#define MQTT_USER "<your_mqtt_user>"
#define MQTT_PASSWORD "<your_mqtt_password>"

/************* MQTT TOPICS (change these topics as you wish)  **************************/
#define MQTT_TEMPERATURE_TOPIC "weather/living_room/temperature"
#define MQTT_HUMIDITY_TOPIC "weather/living_room/humidity"
#define MQTT_PRESSURE_TOPIC "weather/living_room/pressure"
#define MQTT_GAS_RESISTANCE_TOPIC "weather/living_room/gas_resistance"
#define MQTT_ALTITUDE_TOPIC "weather/living_room/altitude"
#define MQTT_INFLUXDB_TOPIC "weather/living_room/telegraf"

/****************************************FOR JSON***************************************/
//const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);
#define MQTT_MAX_PACKET_SIZE 512

/************ WIFI Information (CHANGE THESE FOR YOUR SETUP) ******************/
#define WIFI_SSID "<your_wifi_ssid>"
#define WIFI_PASSWORD "<your_wifi_password>"

/************ Battery settings ******************/
#define BATTERY_SAVING_VOLTAGE 2750.0
#define DEEP_SLEEP_FOR 60 * 20
#define DEEP_POWER_SAVE_SLEEP_FOR 60 * 40

#define LOGGING false

// Pfaffikon ZH
#define SEALEVELPRESSURE_HPA 1033.00

ADC_MODE(ADC_VCC);

Adafruit_BME680 bme; // I2C

WiFiClient net;
PubSubClient client(net);

boolean is_night = false;

float temperature;
float humidity;
float pressure;
float gas_resistance;
float est_altitude;

void on_mqtt_message(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

#ifdef LOGGING
  Serial.print("Current sun state is:");
  Serial.println(message);
#endif

  if (String(message) == "below_horizon") {
#ifdef LOGGING
    Serial.println("Below horizon!");
#endif
    is_night = true;
  } else {
#ifdef LOGGING
    Serial.println("Above horizon!");
#endif
    is_night = false;
  }
}

// Enter in deep bear sleep
void deepSleep () {
#ifdef LOGGING
  Serial.println("Going deep sleep!");
#endif
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  int sleep_time = 60;

  ESP.deepSleep(sleep_time * 1000000);
}

void setupWifi() {
#ifdef LOGGING
  Serial.println("----------");
  Serial.println("Connecting to: ");
  Serial.println(WIFI_SSID);
#endif
  WiFi.mode(WIFI_STA);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef LOGGING
    Serial.print(".");
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    delay(500);
  }
#ifdef LOGGING
  Serial.println("OK!");
  randomSeed(micros());
  printWifiInfo();
#endif
}

boolean mqttReconnect() {
  while (!client.connected()) {
    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setCallback(on_mqtt_message);
    String clientId = "EnvSensor-";
    clientId += String(random(0xffff), HEX);
#ifdef LOGGING
    Serial.print("Attempting MQTT connection, ");
    Serial.print("client id: ");
    Serial.println(clientId);
#endif
    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
#ifdef LOGGING
      Serial.println("Connected!");
#endif
      client.subscribe("home/sun");
      return true;
    } else {
#ifdef LOGGING
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
#endif
      // Wait 5 seconds before retrying
      delay(5000);
      return false;
    }
  }
}

void printWifiInfo() {
#ifdef LOGGING
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif
}

boolean setupSensor() {
  if (!bme.begin()) {
#ifdef LOGGING
    Serial.println("Could not find a valid BME680 sensor, check wiring!");
#endif
    return false;
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  // 320*C for 150 ms
  bme.setGasHeater(320, 150);
  return true;
}

void publish(char * topic, float value) {
  String payload = String(value);
#ifdef LOGGING
  Serial.print("MQTT: Send String payload to topic: ");
  Serial.println(topic);
  Serial.print("MQTT: Send payload: ");
  Serial.println(payload);
#endif
  if (! client.publish(topic, payload.c_str(), false)) {
    Serial.println("MQTT: Failed send float payload");
    return;
  }
}

void publishString(char * topic, String payload) {
#ifdef LOGGING
  Serial.print("MQTT: Send String payload to topic: ");
  Serial.println(topic);
  Serial.print("MQTT: Send payload: ");
  Serial.println(payload);
#endif
  if (! client.publish(topic, payload.c_str(), true)) {
    Serial.println("MQTT: Failed send String payload");
    return;
  }
}

void influxLineProtocol(void){
  
  char influx_line[90];
  char *measure = "weather,location=living_room ";
  char* temp_field = "temp=";
  char* humidity_field = ",hum=";
  char* pressure_field = ",press=";
  char* gas_field = ",gas=";
  char* altitude_field = ",alt=";
  char temperature_value[7];
  char humidity_value[7];
  char pressure_value[8];
  char gas_value[8];
  char altitude_value[8];
  
  dtostrf(temperature, 4, 2, temperature_value);
  dtostrf(humidity, 4, 2, humidity_value);
  dtostrf(pressure, 4, 2, pressure_value);
  dtostrf(gas_resistance, 4, 2, gas_value);
  dtostrf(est_altitude, 4, 2, altitude_value);

  influx_line[0] = '\0';
  strcat(influx_line, measure);
  strcat(influx_line, temp_field);
  strcat(influx_line, temperature_value);
  strcat(influx_line, humidity_field);
  strcat(influx_line, humidity_value);
  strcat(influx_line, pressure_field);
  strcat(influx_line, pressure_value);
  strcat(influx_line, gas_field);
  strcat(influx_line, gas_value);
  strcat(influx_line, altitude_field);
  strcat(influx_line, altitude_value);

#ifdef LOGGING
  Serial.print("MQTT: Send payload to topic: ");
  Serial.println(MQTT_INFLUXDB_TOPIC);
  Serial.print("MQTT: Send payload: ");
  Serial.println(influx_line);
#endif

  if (! client.publish(MQTT_INFLUXDB_TOPIC, influx_line, true)) {
    Serial.println("MQTT: Failed send String payload");
    return;
  }
}

void outputMeasurementConsole(void){
  #ifdef LOGGING
  Serial.print("Current temperature is: ");
  Serial.println(temperature);
  Serial.print("Current humidity is: ");
  Serial.println(humidity);
  Serial.print("Current pressure is: ");
  Serial.println(pressure);
  Serial.print("Current gas is: ");
  Serial.println(gas_resistance);
  Serial.print("Current altitude is: ");
  Serial.println(est_altitude);
#endif
}

void outputMeasurementMqtt(void){
  publish(MQTT_TEMPERATURE_TOPIC, temperature);
  publish(MQTT_HUMIDITY_TOPIC, humidity);
  publish(MQTT_PRESSURE_TOPIC, pressure);
  publish(MQTT_GAS_RESISTANCE_TOPIC, gas_resistance);
  publish(MQTT_ALTITUDE_TOPIC, est_altitude);
  influxLineProtocol();
}

void measurement(void){
  
  if (! bme.performReading()) {
    Serial.println("Failed to read measurement of BME680");
    return;
  }

  temperature    = bme.temperature;
  humidity       = bme.humidity;
  pressure       = bme.pressure / 100.0;
  gas_resistance = bme.gas_resistance / 1000.0;
  est_altitude   = bme.readAltitude(SEALEVELPRESSURE_HPA);

  // Read again to get correct value and not 0
  delay(100);
  gas_resistance = bme.gas_resistance / 1000.0;

  outputMeasurementConsole();
  outputMeasurementMqtt();

  delay(60000);
}

void setup() {
#ifdef LOGGING
  Serial.begin(115200);
#endif
  setupWifi();
  if (! setupSensor()) {
    Serial.println("Failed setup BME680");
    return;
  }
//  deepSleep();
}

void loop() {
  if (mqttReconnect()) {
    measurement();
  }
}
