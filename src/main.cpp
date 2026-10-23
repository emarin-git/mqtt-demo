#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String clientId;
unsigned long counter = 0;
unsigned long lastPublish = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

void setLed(bool on) {
  digitalWrite(LED_PIN, (on != LED_ACTIVE_LOW) ? HIGH : LOW);
}

void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();

  Serial.printf("[MQTT] Mensaje en %s: %s\n", topic, msg.c_str());

  if (msg == "ON") {
    setLed(true);
    Serial.println("[LED] Encendido");
  } else if (msg == "OFF") {
    setLed(false);
    Serial.println("[LED] Apagado");
  } else {
    Serial.println("[LED] Comando ignorado (se espera ON u OFF)");
  }
}

// Gestiona la conexión Wi-Fi sin bloquear. Devuelve true si hay conexión.
bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  unsigned long now = millis();
  if (lastWifiAttempt == 0 || now - lastWifiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    lastWifiAttempt = now;
    Serial.printf("[WiFi] Conectando a %s...\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  return false;
}

// Gestiona la conexión MQTT sin bloquear. Devuelve true si hay conexión.
bool ensureMqtt() {
  if (mqtt.connected()) {
    return true;
  }

  unsigned long now = millis();
  if (lastMqttAttempt != 0 && now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) {
    return false;
  }
  lastMqttAttempt = now;

  Serial.printf("[MQTT] Conectando a %s:%d como %s...\n", MQTT_SERVER, MQTT_PORT, clientId.c_str());

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
  } else {
    ok = mqtt.connect(clientId.c_str());
  }

  if (!ok) {
    Serial.printf("[MQTT] Fallo, estado=%d. Reintentando en %d ms\n", mqtt.state(), MQTT_RETRY_INTERVAL_MS);
    return false;
  }

  Serial.println("[MQTT] Conectado");
  mqtt.subscribe(TOPIC_LED);
  Serial.printf("[MQTT] Suscrito a %s\n", TOPIC_LED);
  return true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  clientId = String(MQTT_CLIENT_PREFIX) + WiFi.macAddress();
  clientId.replace(":", "");

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
}

void loop() {
  static bool wifiWasConnected = false;

  bool wifiOk = ensureWifi();
  if (wifiOk && !wifiWasConnected) {
    Serial.printf("[WiFi] Conectado, IP: %s\n", WiFi.localIP().toString().c_str());
    lastMqttAttempt = 0;  // intentar MQTT de inmediato
  } else if (!wifiOk && wifiWasConnected) {
    Serial.println("[WiFi] Conexión perdida");
  }
  wifiWasConnected = wifiOk;

  if (wifiOk && ensureMqtt()) {
    mqtt.loop();
  }

  // El contador se incrementa cada segundo aunque no haya conexión;
  // solo se publica cuando MQTT está disponible.
  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = now;
    counter++;

    if (mqtt.connected()) {
      char payload[16];
      snprintf(payload, sizeof(payload), "%lu", counter);
      mqtt.publish(TOPIC_COUNTER, payload);
      Serial.printf("[MQTT] %s -> %s\n", TOPIC_COUNTER, payload);
    }
  }
}
