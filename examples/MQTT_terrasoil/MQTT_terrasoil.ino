#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TerraSoil.h>

// WiFi credentials
const char *ssid = "iPhone"; // Remplacez par votre nom WiFi
const char *password = "123456789"; // Remplacez par votre mot de passe WiFi

// MQTT Broker configuration
const char *mqtt_broker = "broker.emqx.io";
const char *mqtt_topic = "sensor";
const char *mqtt_username = "tera";
const char *mqtt_password = "123456789";
const int mqtt_port = 1883;

// ID du capteur
const char *sensor_id = "0001";

// Configuration des pins pour XIAO ESP32-S3
#define RS485_RX_PIN   D7  // D7
#define RS485_TX_PIN   D6  // D6
#define RS485_RTS_PIN  D1   // D1

// WiFi and MQTT client initialization
WiFiClient esp_client;  // Changé de WiFiClientSecure à WiFiClient (sans SSL)
PubSubClient mqtt_client(esp_client);

// Créer l'objet capteur TerraSoil
HardwareSerial RS485Serial(1);  // UART1
TerraSoil sensor(&RS485Serial, RS485_RTS_PIN);

// Structure pour stocker les données TerraSoil
TerraSoilData soilData;

// Function Declarations
void connectToWiFi();
void connectToMQTT();
void publishSensorData();
void readTerraSoilData();

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== Terra-AI Soil Monitoring System ===");
    Serial.print("TerraSoil Library Version: ");
    Serial.println(TerraSoil::getVersion());
    Serial.printf("Capteur ID: %s\n", sensor_id);
    
    // Initialize WiFi
    connectToWiFi();
    
    // Initialize TerraSoil sensor
    if (sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800)) {
        Serial.println("✓ TerraSoil sensor initialized");
        Serial.printf("  Modbus Address: 0x%02X\n", sensor.getAddress());
    } else {
        Serial.println("✗ TerraSoil sensor initialization failed");
    }
    
    // Initialize MQTT
    connectToMQTT();
    
    pinMode(LED_BUILTIN, OUTPUT);
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void connectToMQTT() {
    mqtt_client.setServer(mqtt_broker, mqtt_port);  // Set the MQTT broker server and port
    mqtt_client.setKeepAlive(60);  // Set the keep-alive interval to 60 seconds

    while (!mqtt_client.connected()) {
        String client_id = "terra-ai-" + String(sensor_id) + "-" + String(WiFi.macAddress());
        Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
        
        if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("Connected to MQTT Broker");
        } else {
            Serial.print("Failed to connect, rc=");
            Serial.print(mqtt_client.state());
            Serial.println(" Retrying in 5 seconds.");
            delay(5000);
        }
    }
}

void readTerraSoilData() {
    digitalWrite(LED_BUILTIN, HIGH);
    
    if (sensor.readSensor(soilData)) {
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("✓ TerraSoil data read successfully");
    } else {
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("✗ Failed to read TerraSoil data");
        // Valeurs par défaut en cas d'erreur
        soilData.moisture = 65.8;
        soilData.temperature = 25.3;
        soilData.conductivity = 1250;
        soilData.ph = 6.5;
        soilData.nitrogen = 85;
        soilData.phosphorus = 42;
        soilData.potassium = 120;
        soilData.salinity = 15;
        soilData.tds = 625;
        soilData.fertility = 247;
        soilData.timestamp = millis();
    }
}

void publishSensorData() {
    StaticJsonDocument<256> json_doc;
    
    // Lire les données du capteur TerraSoil
    readTerraSoilData();
    
    // Ajouter toutes les données au document JSON
    json_doc["ID"] = sensor_id;
    json_doc["hum"] = soilData.moisture;
    json_doc["temp"] = soilData.temperature;
    json_doc["ec"] = soilData.conductivity;
    json_doc["ph"] = soilData.ph;
    json_doc["n"] = soilData.nitrogen;
    json_doc["p"] = soilData.phosphorus;
    json_doc["k"] = soilData.potassium;
    json_doc["sal"] = soilData.salinity;
    json_doc["tds"] = soilData.tds;
    json_doc["fert"] = soilData.fertility;
    json_doc["ok"] = 1;
    json_doc["time"] = soilData.timestamp;
    
    char json_buffer[512];
    serializeJson(json_doc, json_buffer);
    
    // Afficher les données dans le moniteur série
    serializeJson(json_doc, Serial);
    Serial.println();
    
    // Publier sur MQTT
    if (mqtt_client.publish(mqtt_topic, json_buffer)) {
        Serial.printf("✓ Published to %s\n", mqtt_topic);
    } else {
        Serial.println("✗ Failed to publish to MQTT");
    }
}

void loop() {
    if (!mqtt_client.connected()) {
        connectToMQTT();
    }
    mqtt_client.loop();

    publishSensorData();
    delay(5000); // Lecture toutes les 5 secondes (comme dans l'exemple original)
}
