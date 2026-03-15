#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "DHT.h"

float TEMP_THRESHOLD = 50.0;
int LIGHT_THRESHOLD = 1000;
int DIST_THRESHOLD = 200;
unsigned long Interval = 2000;

const char* mqtt_server = "broker.hivemq.com";

#define DHTTYPE DHT22

Servo servo;
DHT dht(15, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);
void callback(char* topic, byte* payload, unsigned int length) {
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
 
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  Serial.println("Payload: " + messageTemp);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, messageTemp);
  if (error) {
    Serial.println("Failed to parse JSON");
    return;
  }

  String topicStr = String(topic);

  if (topicStr == "salma/actuators/led") {
    String state = doc["state"];
    String color = doc["color"];
   
    int pin = 12;
    Serial.println("State: " + state);
    Serial.println("Color: " + color);
    if (color == "red") pin = 12;
    else if (color == "yellow") pin = 14;
    else if (color == "green") pin = 26;
   
    digitalWrite(pin, (state == "on") ? HIGH : LOW);
  }
  else if (topicStr == "salma/actuators/buzzer") {
    String state = doc["state"];
    digitalWrite(27, (state == "on") ? HIGH : LOW);
  }
  else if (topicStr == "salma/actuators/servo") {
    int angle = doc["angle"];
    servo.write(angle);
  }
  else if (topicStr == "salma/actuators/relay") {
    String state = doc["state"];
  }

  else if (topicStr == "salma/config/thresholds") {
    if (doc.containsKey("temp_max")) TEMP_THRESHOLD = doc["temp_max"];
    if (doc.containsKey("light_min")) LIGHT_THRESHOLD = doc["light_min"];
    if (doc.containsKey("dist_min")) DIST_THRESHOLD = doc["dist_min"];
    Serial.println("Thresholds updated via Dashboard!");
  }
  else if (topicStr == "salma/config/interval") {
    Serial.println("inside interval");
    if (doc.containsKey("value")) {
      Interval = doc["value"];
      Serial.print("Reporting interval changed to: ");
      Serial.println(Interval);
    }
  }
}
void setupWiFi() {
  delay(100);
  Serial.println("Initializing WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin("Wokwi-GUEST", "");
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    delay(500);
    Serial.print(".");
    attempt++;
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
  } else {
    Serial.println("\nWiFi Failed. Try refreshing your browser.");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("attempting mttq connection...");
    String clientId = "ESP32-Assignment-1-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe("salma/actuators/#");
      client.subscribe("salma/config/#");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
long readDistance() {
  digitalWrite(5, LOW);
  delayMicroseconds(2);
  digitalWrite(5, HIGH);
  delayMicroseconds(10);
  digitalWrite(5, LOW);

  long duration = pulseIn(18, HIGH);
  duration = duration * 0.034 / 2;
  return duration;
}

void setup() {
  Serial.begin(115200);
  setupWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  pinMode(13, INPUT);
  pinMode(12, OUTPUT);
  pinMode(14, OUTPUT);
  pinMode(27, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(18, INPUT);
  pinMode(26, OUTPUT);
  digitalWrite(26, HIGH);
  servo.attach(19);
  servo.write(0);
  dht.begin();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int lightLevel = analogRead(34);
  int motionDetected = digitalRead(13);
  long distance = readDistance();

  static unsigned long lastMsg = 0;
  static unsigned long lastStatus = 0;
  if (millis() - lastMsg > Interval) {
    lastMsg = millis();
    
    if (!isnan(h) && !isnan(t)) {
      StaticJsonDocument<200> doc;
      doc["temperature"] = t;
      doc["humidity"] = h;
      doc["light"] = lightLevel;
      doc["motion"] = (motionDetected == HIGH);

      char buffer[200];
      serializeJson(doc, buffer);
      
      String tempJson = "{\"value\": " + String(t) + ", \"unit\": \"°C\"}";
      client.publish("salma/sensors/temperature", tempJson.c_str());

      String humJson = "{\"value\": " + String(h) + ", \"unit\": \"%\"}";
      client.publish("salma/sensors/humidity", humJson.c_str());

      String lightLevelText = (lightLevel < LIGHT_THRESHOLD) ? "dark" : "bright";
      String lightJson = "{\"value\": " + String(lightLevel) + ", \"level\": \"" + lightLevelText + "\"}";
      client.publish("salma/sensors/light", lightJson.c_str());

      String motionJson = (motionDetected == HIGH) ? "{\"detected\": true}" : "{\"detected\": false}";
      client.publish("salma/sensors/motion", motionJson.c_str());
      String distJson = "{\"value\": " + String(distance) + ", \"unit\": \"cm\"}";
      client.publish("salma/sensors/distance", distJson.c_str());
     
      Serial.println("Published JSON payloads successfully.");

      client.publish("salma/sensors/all", buffer);

      Serial.println("Published: " + String(buffer));
      if (t > TEMP_THRESHOLD) {
        digitalWrite(12, HIGH);
      } else {
        digitalWrite(12, LOW);
      }

      digitalWrite(14, (lightLevel < LIGHT_THRESHOLD) ? HIGH : LOW);

      if (motionDetected == HIGH) {
        digitalWrite(27, HIGH);
        delay(2000);
        digitalWrite(27, LOW);
      }

      if (distance < DIST_THRESHOLD && distance > 0) {
        servo.write(90);
      } else {
        servo.write(0);
      }
    }
  }
  if (millis() - lastStatus > 10000) {
    lastStatus = millis();

    StaticJsonDocument<200> statusDoc;
    statusDoc["uptime"] = millis() / 1000;
    statusDoc["rssi"] = WiFi.RSSI();
    statusDoc["free_heap"] = ESP.getFreeHeap();
    statusDoc["status"] = "online";

    char statusBuffer[200];
    serializeJson(statusDoc, statusBuffer);
    client.publish("salma/system/status", statusBuffer);

    Serial.print("System status: ");
    Serial.println(statusBuffer);
  }


  delay(100); 
}