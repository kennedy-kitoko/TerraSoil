/*
 * TerraSoil Library
 * 
 * Bibliothèque pour capteur NPK Soil Sensor RS485 (SN-300*-TR-*-N01)
 * Compatible avec XIAO ESP32-S3 et autres microcontrôleurs ESP32
 * 
 * Auteur: TerraSoil Team
 * Version: 1.0.0
 * Date: 2024
 * 
 * Lecture des 10 paramètres du sol :
 * - Humidité (%)
 * - Température (°C)
 * - Conductivité électrique (µS/cm)
 * - pH
 * - Azote N (mg/kg)
 * - Phosphore P (mg/kg)
 * - Potassium K (mg/kg)
 * - Salinité
 * - TDS - Total Dissolved Solids (mg/L)
 * - Fertilité (mg/kg)
 */

#ifndef TERRASOIL_H
#define TERRASOIL_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Version de la bibliothèque
#define TERRASOIL_VERSION "1.0.0"

// Valeurs par défaut
#define TERRASOIL_DEFAULT_BAUD 4800
#define TERRASOIL_DEFAULT_ADDRESS 0x01
#define TERRASOIL_DEFAULT_TIMEOUT 300

// Registres Modbus du capteur NPK
#define TERRASOIL_REG_MOISTURE     0x0000  // Humidité
#define TERRASOIL_REG_TEMPERATURE  0x0001  // Température
#define TERRASOIL_REG_CONDUCTIVITY 0x0002  // Conductivité
#define TERRASOIL_REG_PH           0x0003  // pH
#define TERRASOIL_REG_NITROGEN     0x0004  // Azote
#define TERRASOIL_REG_PHOSPHORUS   0x0005  // Phosphore
#define TERRASOIL_REG_POTASSIUM    0x0006  // Potassium
#define TERRASOIL_REG_SALINITY     0x0007  // Salinité
#define TERRASOIL_REG_TDS          0x0008  // TDS
#define TERRASOIL_REG_FERTILITY    0x000C  // Fertilité

/**
 * @brief Structure contenant toutes les données du capteur
 */
struct TerraSoilData {
  float moisture;        // Humidité du sol (%)
  float temperature;     // Température (°C)
  uint16_t conductivity; // Conductivité électrique (µS/cm)
  float ph;              // pH du sol
  uint16_t nitrogen;     // Azote N (mg/kg)
  uint16_t phosphorus;   // Phosphore P (mg/kg)
  uint16_t potassium;    // Potassium K (mg/kg)
  uint16_t salinity;     // Salinité
  uint16_t tds;          // TDS (mg/L)
  uint16_t fertility;    // Fertilité (mg/kg)
  bool success;          // Statut de lecture
  uint32_t timestamp;    // Timestamp de la lecture
};

/**
 * @brief Classe principale pour le capteur NPK Soil Sensor
 */
class TerraSoil {
public:
  /**
   * @brief Constructeur
   * @param serial Pointeur vers HardwareSerial (UART)
   * @param rtsPin Pin de contrôle DE/RE du module RS485
   * @param address Adresse Modbus du capteur (défaut: 0x01)
   */
  TerraSoil(HardwareSerial* serial, uint8_t rtsPin, uint8_t address = TERRASOIL_DEFAULT_ADDRESS);
  
  /**
   * @brief Initialiser la communication RS485
   * @param rxPin Pin RX
   * @param txPin Pin TX
   * @param baud Vitesse de communication (défaut: 4800)
   * @return true si succès
   */
  bool begin(uint8_t rxPin, uint8_t txPin, uint32_t baud = TERRASOIL_DEFAULT_BAUD);
  
  /**
   * @brief Lire toutes les données du capteur (FONCTION PRINCIPALE)
   * @param data Structure où stocker les données
   * @return true si toutes les lectures ont réussi
   */
  bool readSensor(TerraSoilData &data);
  
  /**
   * @brief Lire un registre spécifique
   * @param regAddress Adresse du registre
   * @param value Valeur lue
   * @return true si succès
   */
  bool readRegister(uint16_t regAddress, uint16_t &value);
  
  /**
   * @brief Définir le délai entre les lectures
   * @param delayMs Délai en millisecondes (défaut: 50ms)
   */
  void setReadDelay(uint16_t delayMs);
  
  /**
   * @brief Définir le timeout de communication
   * @param timeoutMs Timeout en millisecondes (défaut: 300ms)
   */
  void setTimeout(uint16_t timeoutMs);
  
  /**
   * @brief Obtenir l'adresse Modbus du capteur
   * @return Adresse configurée
   */
  uint8_t getAddress() const;
  
  /**
   * @brief Obtenir la version de la bibliothèque
   * @return String contenant la version
   */
  static const char* getVersion();

private:
  HardwareSerial* _serial;  // Port série RS485
  uint8_t _rtsPin;          // Pin DE/RE control
  uint8_t _address;         // Adresse Modbus
  uint16_t _readDelay;      // Délai entre lectures (ms)
  uint16_t _timeout;        // Timeout communication (ms)
  
  /**
   * @brief Calculer le CRC16 Modbus
   * @param buffer Buffer de données
   * @param length Longueur du buffer
   * @return CRC calculé
   */
  uint16_t calculateCRC(const uint8_t *buffer, uint8_t length);
  
  /**
   * @brief Envoyer une requête Modbus
   * @param request Buffer de la requête
   * @param length Longueur de la requête
   */
  void sendRequest(const uint8_t *request, uint8_t length);
  
  /**
   * @brief Recevoir une réponse Modbus
   * @param response Buffer pour la réponse
   * @param expectedLength Longueur attendue
   * @return Nombre d'octets reçus
   */
  uint8_t receiveResponse(uint8_t *response, uint8_t expectedLength);
};

#endif // TERRASOIL_H
