/*
 * TerraSoil Library - Implementation
 * 
 * Bibliothèque pour capteur NPK Soil Sensor RS485 (SN-300*-TR-*-N01)
 */

#include "TerraSoil.h"

/**
 * Constructeur
 */
TerraSoil::TerraSoil(HardwareSerial* serial, uint8_t rtsPin, uint8_t address) {
  _serial = serial;
  _rtsPin = rtsPin;
  _address = address;
  _readDelay = 50;  // 50ms par défaut entre lectures
  _timeout = TERRASOIL_DEFAULT_TIMEOUT;
}

/**
 * Initialiser la communication RS485
 */
bool TerraSoil::begin(uint8_t rxPin, uint8_t txPin, uint32_t baud) {
  // Configuration pin RTS
  pinMode(_rtsPin, OUTPUT);
  digitalWrite(_rtsPin, LOW);  // Mode réception par défaut
  
  // Initialiser le port série
  _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
  
  // Petit délai pour stabilisation
  delay(100);
  
  return true;
}

/**
 * Calculer CRC16 Modbus
 */
uint16_t TerraSoil::calculateCRC(const uint8_t *buffer, uint8_t length) {
  uint16_t crc = 0xFFFF;
  
  for (uint8_t i = 0; i < length; i++) {
    crc ^= buffer[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  
  return crc;
}

/**
 * Envoyer une requête Modbus
 */
void TerraSoil::sendRequest(const uint8_t *request, uint8_t length) {
  // Vider le buffer de réception
  while (_serial->available()) {
    _serial->read();
  }
  
  // Activer le mode transmission
  digitalWrite(_rtsPin, HIGH);
  delayMicroseconds(10);
  
  // Envoyer la requête
  _serial->write(request, length);
  _serial->flush();
  
  // Retour en mode réception
  delayMicroseconds(10);
  digitalWrite(_rtsPin, LOW);
}

/**
 * Recevoir une réponse Modbus
 */
uint8_t TerraSoil::receiveResponse(uint8_t *response, uint8_t expectedLength) {
  uint8_t bytesRead = 0;
  unsigned long startTime = millis();
  
  while (millis() - startTime < _timeout && bytesRead < expectedLength) {
    if (_serial->available()) {
      response[bytesRead++] = _serial->read();
    }
  }
  
  return bytesRead;
}

/**
 * Lire un registre Modbus
 */
bool TerraSoil::readRegister(uint16_t regAddress, uint16_t &value) {
  uint8_t request[8];
  
  // Construire la requête Modbus RTU
  request[0] = _address;              // Adresse esclave
  request[1] = 0x03;                  // Fonction: Read Holding Registers
  request[2] = regAddress >> 8;       // Adresse registre (MSB)
  request[3] = regAddress & 0xFF;     // Adresse registre (LSB)
  request[4] = 0x00;                  // Nombre de registres (MSB)
  request[5] = 0x01;                  // Nombre de registres (LSB) = 1
  
  // Calculer et ajouter le CRC
  uint16_t crc = calculateCRC(request, 6);
  request[6] = crc & 0xFF;            // CRC (LSB)
  request[7] = crc >> 8;              // CRC (MSB)
  
  // Envoyer la requête
  sendRequest(request, 8);
  
  // Attendre et recevoir la réponse
  delay(100);  // Délai pour que le capteur réponde
  
  uint8_t response[7];
  uint8_t bytesRead = receiveResponse(response, 7);
  
  // Vérifier la réponse
  if (bytesRead == 7 && 
      response[0] == _address && 
      response[1] == 0x03) {
    
    // Vérifier le CRC
    uint16_t receivedCRC = response[5] | (response[6] << 8);
    uint16_t calculatedCRC = calculateCRC(response, 5);
    
    if (receivedCRC == calculatedCRC) {
      // Extraire la valeur (Big Endian)
      value = (response[3] << 8) | response[4];
      return true;
    }
  }
  
  return false;
}

/**
 * FONCTION PRINCIPALE : Lire toutes les données du capteur
 */
bool TerraSoil::readSensor(TerraSoilData &data) {
  uint16_t rawValue;
  bool allSuccess = true;
  
  // Timestamp de la lecture
  data.timestamp = millis();
  
  // 1. Humidité (0x0000) - divisé par 10
  if (readRegister(TERRASOIL_REG_MOISTURE, rawValue)) {
    data.moisture = rawValue / 10.0;
  } else {
    data.moisture = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 2. Température (0x0001) - divisé par 10, gestion valeurs négatives
  if (readRegister(TERRASOIL_REG_TEMPERATURE, rawValue)) {
    if (rawValue & 0x8000) {  // Test si négatif
      int16_t temp = (int16_t)rawValue;
      data.temperature = temp / 10.0;
    } else {
      data.temperature = rawValue / 10.0;
    }
  } else {
    data.temperature = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 3. Conductivité (0x0002)
  if (readRegister(TERRASOIL_REG_CONDUCTIVITY, rawValue)) {
    data.conductivity = rawValue;
  } else {
    data.conductivity = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 4. pH (0x0003) - divisé par 10
  if (readRegister(TERRASOIL_REG_PH, rawValue)) {
    data.ph = rawValue / 10.0;
  } else {
    data.ph = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 5. Azote (0x0004)
  if (readRegister(TERRASOIL_REG_NITROGEN, rawValue)) {
    data.nitrogen = rawValue;
  } else {
    data.nitrogen = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 6. Phosphore (0x0005)
  if (readRegister(TERRASOIL_REG_PHOSPHORUS, rawValue)) {
    data.phosphorus = rawValue;
  } else {
    data.phosphorus = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 7. Potassium (0x0006)
  if (readRegister(TERRASOIL_REG_POTASSIUM, rawValue)) {
    data.potassium = rawValue;
  } else {
    data.potassium = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 8. Salinité (0x0007)
  if (readRegister(TERRASOIL_REG_SALINITY, rawValue)) {
    data.salinity = rawValue;
  } else {
    data.salinity = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 9. TDS (0x0008)
  if (readRegister(TERRASOIL_REG_TDS, rawValue)) {
    data.tds = rawValue;
  } else {
    data.tds = 0;
    allSuccess = false;
  }
  delay(_readDelay);
  
  // 10. Fertilité (0x000C)
  if (readRegister(TERRASOIL_REG_FERTILITY, rawValue)) {
    data.fertility = rawValue;
  } else {
    data.fertility = 0;
    allSuccess = false;
  }
  
  // Statut final
  data.success = allSuccess;
  
  return allSuccess;
}

/**
 * Définir le délai entre lectures
 */
void TerraSoil::setReadDelay(uint16_t delayMs) {
  _readDelay = delayMs;
}

/**
 * Définir le timeout
 */
void TerraSoil::setTimeout(uint16_t timeoutMs) {
  _timeout = timeoutMs;
}

/**
 * Obtenir l'adresse Modbus
 */
uint8_t TerraSoil::getAddress() const {
  return _address;
}

/**
 * Obtenir la version
 */
const char* TerraSoil::getVersion() {
  return TERRASOIL_VERSION;
}
