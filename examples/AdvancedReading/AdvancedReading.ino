/*
 * author: KITOKO MUYUNGA KENNEDY
 * https://github.com/kennedy-kitoko/TERRA-AI
 * 
 * Exemple avancé - TerraSoil Library
 * 
 * Lecture avec affichage détaillé formaté
 * Démonstration de toutes les fonctionnalités
 * 
 * Câblage pour XIAO ESP32-S3:
 * - RS485 RX  → GPIO44 (D7)
 * - RS485 TX  → GPIO43 (D6)
 * - RS485 DE/RE → GPIO1 (D1)
 */

#include <TerraSoil.h>

// Configuration des pins
#define RS485_RX_PIN   44  // D7
#define RS485_TX_PIN   43  // D6
#define RS485_RTS_PIN  1   // D1

// Créer l'objet capteur
HardwareSerial RS485Serial(1);
TerraSoil sensor(&RS485Serial, RS485_RTS_PIN);

TerraSoilData data;

void printSeparator() {
  Serial.println("═══════════════════════════════════════════════");
}

void printSensorData() {
  printSeparator();
  Serial.println("         🌱 DONNÉES DU CAPTEUR NPK 🌱");
  printSeparator();
  
  Serial.println("\n📊 PARAMÈTRES PHYSIQUES:");
  Serial.printf("   💧 Humidité:        %.1f %%\n", data.moisture);
  Serial.printf("   🌡️  Température:     %.1f °C\n", data.temperature);
  Serial.printf("   ⚡ Conductivité:    %d µS/cm\n", data.conductivity);
  Serial.printf("   🧪 pH:              %.1f\n", data.ph);
  
  Serial.println("\n🧬 NUTRIMENTS (NPK):");
  Serial.printf("   🔴 Azote (N):       %d mg/kg\n", data.nitrogen);
  Serial.printf("   🟠 Phosphore (P):   %d mg/kg\n", data.phosphorus);
  Serial.printf("   🟡 Potassium (K):   %d mg/kg\n", data.potassium);
  
  Serial.println("\n📈 PARAMÈTRES CALCULÉS:");
  Serial.printf("   🧂 Salinité:        %d\n", data.salinity);
  Serial.printf("   💎 TDS:             %d mg/L\n", data.tds);
  Serial.printf("   🌾 Fertilité:       %d mg/kg\n", data.fertility);
  
  Serial.println("\n⏱️  MÉTADONNÉES:");
  Serial.printf("   Timestamp:         %lu ms\n", data.timestamp);
  Serial.printf("   Statut:            %s\n", data.success ? "✓ OK" : "✗ ERREUR");
  
  printSeparator();
  Serial.println();
}

void printErrorData() {
  printSeparator();
  Serial.println("         ❌ ERREUR DE LECTURE");
  printSeparator();
  Serial.println("\nVérifiez:");
  Serial.println("  • Connexions RS485 (A/B)");
  Serial.println("  • Alimentation capteur");
  Serial.println("  • Adresse Modbus");
  Serial.println("  • Câblage DE/RE");
  printSeparator();
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  printSeparator();
  Serial.println("    🌍 TerraSoil Library - Advanced Example");
  printSeparator();
  Serial.print("Version: ");
  Serial.println(TerraSoil::getVersion());
  Serial.println();
  
  // Configuration personnalisée
  sensor.setReadDelay(50);    // 50ms entre lectures
  sensor.setTimeout(300);      // 300ms timeout
  
  // Initialiser le capteur
  Serial.println("🔧 Initialisation...");
  if (sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800)) {
    Serial.println("✓ Capteur RS485 initialisé");
    Serial.printf("  • RX Pin: GPIO%d\n", RS485_RX_PIN);
    Serial.printf("  • TX Pin: GPIO%d\n", RS485_TX_PIN);
    Serial.printf("  • RTS Pin: GPIO%d\n", RS485_RTS_PIN);
    Serial.printf("  • Baud: 4800\n");
    Serial.printf("  • Adresse Modbus: 0x%02X\n", sensor.getAddress());
    Serial.println("\n📡 Démarrage des lectures (toutes les 5 secondes)...\n");
  } else {
    Serial.println("✗ Erreur initialisation capteur");
  }
  
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static unsigned long lastRead = 0;
  const unsigned long interval = 5000;
  
  if (millis() - lastRead >= interval) {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("🔄 Lecture en cours...");
    
    // LECTURE DU CAPTEUR
    if (sensor.readSensor(data)) {
      digitalWrite(LED_BUILTIN, LOW);
      printSensorData();
    } else {
      digitalWrite(LED_BUILTIN, LOW);
      printErrorData();
    }
    
    lastRead = millis();
  }
  
  delay(100);
}
