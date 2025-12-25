/*author: KITOKO MUYUNGA KENNEDY
 * https://github.com/kennedy-kitoko/TERRA-AI
 * 
 * Exemple basique - TerraSoil Library
 * 
 * Lecture simple des 10 paramètres du capteur NPK
 * Sortie JSON compacte sur le port série
 * 
 * Câblage pour XIAO ESP32-S3:
 * - RS485 RX  → GPIO44 (D7)
 * - RS485 TX  → GPIO43 (D6)
 * - RS485 DE/RE → GPIO1 (D1)
 * - VCC → 5V
 * - GND → GND (masse commune)
 */

#include <TerraSoil.h>
#include <ArduinoJson.h>

// Configuration des pins pour XIAO ESP32-S3
#define RS485_RX_PIN   44  // D7
#define RS485_TX_PIN   43  // D6
#define RS485_RTS_PIN  1   // D1

// Créer l'objet capteur
HardwareSerial RS485Serial(1);  // UART1
TerraSoil sensor(&RS485Serial, RS485_RTS_PIN);

// Structure pour stocker les données
TerraSoilData data;

void setup() {
  // Démarrer le Serial Monitor
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== TerraSoil Library - Example ===");
  Serial.print("Version: ");
  Serial.println(TerraSoil::getVersion());
  Serial.println("==================================\n");
  
  // Initialiser le capteur
  if (sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800)) {
    Serial.println("✓ Capteur initialisé");
    Serial.printf("  Adresse Modbus: 0x%02X\n", sensor.getAddress());
    Serial.println("  Lecture toutes les 5 secondes...\n");
  } else {
    Serial.println("✗ Erreur initialisation");
  }
  
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static unsigned long lastRead = 0;
  const unsigned long interval = 5000;  // 5 secondes
  
  if (millis() - lastRead >= interval) {
    digitalWrite(LED_BUILTIN, HIGH);
    
    // LECTURE DU CAPTEUR - FONCTION PRINCIPALE
    if (sensor.readSensor(data)) {
      digitalWrite(LED_BUILTIN, LOW);
      
      // Créer JSON compact
      StaticJsonDocument<256> doc;
      doc["hum"] = data.moisture;
      doc["temp"] = data.temperature;
      doc["ec"] = data.conductivity;
      doc["ph"] = data.ph;
      doc["n"] = data.nitrogen;
      doc["p"] = data.phosphorus;
      doc["k"] = data.potassium;
      doc["sal"] = data.salinity;
      doc["tds"] = data.tds;
      doc["fert"] = data.fertility;
      doc["ok"] = 1;
      doc["time"] = data.timestamp;
      
      // Sortie JSON sur une ligne
      serializeJson(doc, Serial);
      Serial.println();
      
    } else {
      digitalWrite(LED_BUILTIN, LOW);
      
      // JSON d'erreur
      StaticJsonDocument<64> doc;
      doc["error"] = "read_failed";
      doc["ok"] = 0;
      serializeJson(doc, Serial);
      Serial.println();
    }
    
    lastRead = millis();
  }
  
  delay(100);
}
