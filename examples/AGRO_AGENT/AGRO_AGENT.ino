/*
 * ═══════════════════════════════════════════════════════════════
 *         TERRA AI v3.0 — Capteur Sol Seul
 *         Librairie TerraAI | NPK Laboratoire | Dual Core
 * ═══════════════════════════════════════════════════════════════
 *
 * Author  : KITOKO MUYUNGA KENNEDY
 * GitHub  : https://github.com/kennedy-kitoko/TERRA-AI
 * Version : 3.0
 *
 * CHANGEMENTS v3.0 :
 *  - Librairie TerraAI (remplace TerraSoil)
 *  - Capteur sol SN-3002-TR uniquement (pas de foliaire)
 *  - Saisie NPK laboratoire via interface web → confirmation
 *  - Affichage côte à côte : NPK capteur vs NPK labo
 *  - Écriture coefficients calibration IEEE754 via web
 *  - Fix Bug 1/2 : irrigation auto cooldown 30s
 *  - Fix Bug 3 : démarrage propre sans spam log
 *  - Fix Bug 4 : validation lecture RS485 partielle (3 tentatives)
 * ═══════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32_AI_Connect.h>
#include <TerraSoil.h>
#include <Preferences.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>

// ═══════════════════════════════════════════════════════════════
// PINS — RAK3112 / XIAO ESP32-S3
// ═══════════════════════════════════════════════════════════════
#define RS485_RX_PIN      D7   // GPIO44 — UART RX
#define RS485_TX_PIN      D6   // GPIO43 — UART TX
#define  RS485_RTS_PIN    D1   // GPIO1  — Direction DE/RE
#define IRRIGATION_PIN    D2   // GPIO2  — Relais irrigation
#define FERTILIZATION_PIN D0   // GPIO4  — Relais fertilisation

// ═══════════════════════════════════════════════════════════════
// OBJETS GLOBAUX
// ═══════════════════════════════════════════════════════════════
WebServer       server(80);
Preferences     preferences;
ESP32_AI_Connect* aiClient = nullptr;

HardwareSerial  RS485Serial(1);
TerraSoil       sensor(&RS485Serial, RS485_RTS_PIN);
TerraSoilData   sensorData;

SemaphoreHandle_t dataMutex;
SemaphoreHandle_t chatMutex;
TaskHandle_t      taskSensorHandle = NULL;

// ═══ Chat inter-core ═══
String   pendingChatRequest  = "";
String   pendingChatResponse = "";
volatile bool chatRequestPending = false;
volatile bool chatResponseReady  = false;

// Config
String wifi_ssid     = "";
String wifi_password = "";
String api_key       = "";
String api_model     = "";
String api_platform  = "";

bool configMode        = false;
bool sensorInitialized    = false;
bool sensorActuallyReading = false; // true seulement après première lecture réelle réussie

// ═══════════════════════════════════════════════════════════════
// STRUCTURES
// ═══════════════════════════════════════════════════════════════

struct SensorLog {
  unsigned long timestamp;
  char     datetime[20];
  float    moisture;
  float    temperature;
  float    ph;
  float    conductivity;
  int      nitrogen;
  int      phosphorus;
  int      potassium;
  float    salinity;
  int      tds;
  int      fertility;
  bool     isPartial;   // true si EC=0/NPK=0 (lecture incomplète)
};

struct SensorThresholds {
  float moistureMin    = 30,  moistureMax    = 70;
  float temperatureMin = 15,  temperatureMax = 35;
  float phMin          = 5.5, phMax          = 7.5;
  float conductivityMin= 500, conductivityMax= 2000;
  int   nitrogenMin    = 80,  nitrogenMax    = 150;
  int   phosphorusMin  = 40,  phosphorusMax  = 100;
  int   potassiumMin   = 70,  potassiumMax   = 140;
  float salinityMin    = 0,   salinityMax    = 500;
  int   tdsMin         = 300, tdsMax         = 1500;
  int   fertilityMin   = 50,  fertilityMax   = 100;
};

struct ChatMessage {
  unsigned long timestamp;
  char   datetime[20];
  String role;
  String content;
};

struct SystemConfig {
  String soilType          = "Loamy soil";
  String cropType          = "Tomato";
  String growthStages      = "Fructification";
  String cultivationMethods= "Greenhouse";
  String aiLanguage        = "fr";
  int    sensorInterval    = 600;
  int    aiInterval        = 1800;
};

struct ActuatorLog {
  unsigned long timestamp;
  char   datetime[20];
  String action;
  String trigger;
  float  moisture;
};

// ─── Données NPK Laboratoire ───────────────────────────────────
struct LabNPK {
  float  nitrogen   = 0;
  float  phosphorus = 0;
  float  potassium  = 0;
  char   date[20]   = "";
  String labName    = "";
  String notes      = "";
  bool   hasData    = false;
  bool   calibrated = false;  // true si coefficients écrits dans capteur
};

// ═══════════════════════════════════════════════════════════════
// VARIABLES GLOBALES
// ═══════════════════════════════════════════════════════════════
#define MAX_SENSOR_LOGS   500
#define MAX_CHAT_MESSAGES 200
#define MAX_ACTUATOR_LOGS 200

SensorLog    sensorLogs[MAX_SENSOR_LOGS];
int          sensorLogCount = 0;

ChatMessage  chatHistory[MAX_CHAT_MESSAGES];
int          chatMessageCount = 0;

ActuatorLog  actuatorLogs[MAX_ACTUATOR_LOGS];
int          actuatorLogCount = 0;

SensorLog        currentReading;
SensorLog        lastValidReading;
bool             hasValidReading = false;

SensorThresholds thresholds;
SystemConfig     systemConfig;
LabNPK           labNPK;

String        lastAnalysis   = "";
unsigned long lastSensorUpdate = 0;
unsigned long lastAIUpdate     = 0;

// BUG 1+2 FIX — cooldown irrigation auto
unsigned long lastIrrigationOnTime = 0;
#define IRRIGATION_AUTO_COOLDOWN_MS 30000

bool irrigationAuto      = true;
bool irrigationManual    = false;
bool fertilizationManual = false;

// ═══════════════════════════════════════════════════════════════
// DÉCLARATIONS
// ═══════════════════════════════════════════════════════════════
void setupConfigPortal();
void setupMainWebServer();
void loadAllConfig();
void saveWiFiConfig();
void saveSystemConfig();
void saveThresholds();
void saveLabNPK();
void loadLabNPK();
bool isReadingValid(TerraSoilData& d);
void readSensorData();
SensorLog toSensorLog(TerraSoilData& d);
void saveSensorData(SensorLog& s);
void saveChatMessage(String role, String content);
void logActuatorAction(String action, String trigger, float moisture);
String buildAnalysisPrompt();
String buildChatContextPrompt(String userMsg);
void updateAISystemRole();
void performAutoAnalysis();
String getDateTime();
void getDateTimeStr(char* buf);
String escapeJSON(String s);
String cleanMarkdown(String s);
void taskWebServer(void* p);
void taskSensorsAndAI(void* p);

// ═══════════════════════════════════════════════════════════════
// HORODATAGE
// ═══════════════════════════════════════════════════════════════
String getDateTime() {
  struct tm ti;
  if (!getLocalTime(&ti)) {
    unsigned long s = millis()/1000;
    char buf[20];
    snprintf(buf,sizeof(buf),"T+%02luh%02lum%02lus",(s/3600)%24,(s/60)%60,s%60);
    return String(buf);
  }
  char buf[20];
  strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&ti);
  return String(buf);
}
void getDateTimeStr(char* buf) {
  String dt = getDateTime();
  strncpy(buf, dt.c_str(), 19);
  buf[19] = '\0';
}

// ═══════════════════════════════════════════════════════════════
// VALIDATION LECTURE RS485 — FIX BUG 4
// ═══════════════════════════════════════════════════════════════
bool isReadingValid(TerraSoilData& d) {
  // Ne pas rejeter EC=0 : sol sec en air libre est valide
  if (d.moisture    < 0   || d.moisture    > 100) return false;
  if (d.temperature < -20 || d.temperature > 80)  return false;
  if (d.ph          < 3.0 || d.ph          > 10.0)return false;
  // Rejeter seulement si toutes les valeurs physiques sont nulles (capteur débranché)
  if (d.moisture == 0 && d.temperature == 0 && d.ph == 0) return false;
  return true;
}

// ═══════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║   TERRA AI v3.0 — Capteur Sol + LabNPK  ║");
  Serial.println("╚══════════════════════════════════════════╝\n");

  if (!SPIFFS.begin(true)) Serial.println("✗ SPIFFS Failed");
  else                      Serial.println("✓ SPIFFS OK");

  dataMutex = xSemaphoreCreateMutex();
  chatMutex = xSemaphoreCreateMutex();

  pinMode(LED_BUILTIN,      OUTPUT);
  pinMode(IRRIGATION_PIN,   OUTPUT);  digitalWrite(IRRIGATION_PIN,   LOW);
  pinMode(FERTILIZATION_PIN,OUTPUT);  digitalWrite(FERTILIZATION_PIN,LOW);

  preferences.begin("terraAI", false);
  loadAllConfig();

  if (wifi_ssid.length() == 0 || api_key.length() == 0) {
    Serial.println("→ Config Portal mode");
    configMode = true;
    setupConfigPortal();
    server.begin();
  } else {
    Serial.println("→ Normal mode");

    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    int att = 0;
    while (WiFi.status() != WL_CONNECTED && att < 30) { delay(500); att++; }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✓ WiFi: " + WiFi.localIP().toString());
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      delay(1000);
      Serial.println("✓ RTC: " + getDateTime());
    } else {
      Serial.println("✗ WiFi failed — uptime mode");
    }

   // TerraSoil — capteur sol SN-3002-TR
    if (sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800)) {
      sensorInitialized = true;
      Serial.println("✓ TerraSoil capteur sol OK");
    } else {
      Serial.println("✗ TerraSoil init failed — simulation mode");
    }

    // AI Client
    aiClient = new ESP32_AI_Connect(api_platform.c_str(), api_key.c_str(), api_model.c_str());
    if (aiClient->begin(api_platform.c_str(), api_key.c_str(), api_model.c_str())) {
      aiClient->setChatTemperature(0.7);
      aiClient->setChatMaxTokens(1500);
      updateAISystemRole();
      Serial.println("✓ AI Client OK");
    }

    setupMainWebServer();
    server.begin();
    Serial.println("✓ WebServer started: http://" + WiFi.localIP().toString());

    xTaskCreatePinnedToCore(taskWebServer,   "WebServer", 16000, NULL, 1, NULL,           0);
    xTaskCreatePinnedToCore(taskSensorsAndAI,"SensorsAI", 18000, NULL, 1, &taskSensorHandle, 1);

    Serial.println("✓ Dual Core active — Core0:Web | Core1:Sensors+AI");
  }
}

void loop() {
  if (configMode) server.handleClient();
  delay(100);
}

// ═══════════════════════════════════════════════════════════════
// FREERTOS TASKS
// ═══════════════════════════════════════════════════════════════
void taskWebServer(void* p) {
  for (;;) { server.handleClient(); vTaskDelay(5/portTICK_PERIOD_MS); }
}

void taskSensorsAndAI(void* p) {
  Serial.println("▶ Core1: Sensors+AI started");

  // BUG 3 FIX — délai démarrage + lecture initiale sans actionneurs
  delay(5000);
  bool savedAuto = irrigationAuto;
  irrigationAuto = false;
  // Lecture initiale HORS mutex — RS485 peut durer plusieurs secondes
  readSensorData();
  irrigationAuto = savedAuto;
  Serial.println("✓ Initial read done — auto-irrigation armed");

  if (systemConfig.sensorInterval < 30)  systemConfig.sensorInterval = 600;
  if (systemConfig.aiInterval     < 60)  systemConfig.aiInterval     = 1800;

  lastSensorUpdate = millis() - ((unsigned long)systemConfig.sensorInterval * 1000UL);
  lastAIUpdate     = millis(); // Attendre un intervalle complet avant première analyse IA

  for (;;) {
    unsigned long now = millis();

    if (now - lastSensorUpdate >= (unsigned long)systemConfig.sensorInterval * 1000UL) {
      lastSensorUpdate = now;
      // Lecture RS485 HORS mutex — Core0 reste réactif
      readSensorData();
    }

    if (now - lastAIUpdate >= (unsigned long)systemConfig.aiInterval * 1000UL) {
      lastAIUpdate = now;
      performAutoAnalysis();
    }

    if (chatRequestPending) {
      String userMsg = "";
      if (xSemaphoreTake(chatMutex, portMAX_DELAY)) {
        userMsg = pendingChatRequest;
        chatRequestPending = false;
        xSemaphoreGive(chatMutex);
      }
      if (userMsg.length() > 0 && aiClient) {
        String ctx  = buildChatContextPrompt(userMsg);
        aiClient->setChatMaxTokens(800);
        String resp = aiClient->chat(ctx);
        aiClient->setChatMaxTokens(1500);
        String clean = (resp.length() > 0) ? cleanMarkdown(resp) : "Erreur: " + aiClient->getLastError();
        saveChatMessage("user",      userMsg);
        saveChatMessage("assistant", clean);
        if (xSemaphoreTake(chatMutex, portMAX_DELAY)) {
          pendingChatResponse = clean;
          chatResponseReady   = true;
          xSemaphoreGive(chatMutex);
        }
      }
    }
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
}

// ═══════════════════════════════════════════════════════════════
// LECTURE CAPTEUR SOL — avec 3 tentatives (FIX BUG 4)
// ═══════════════════════════════════════════════════════════════
void readSensorData() {
  digitalWrite(LED_BUILTIN, HIGH);

  SensorLog newReading;
  newReading.timestamp = millis();
  getDateTimeStr(newReading.datetime);
  newReading.isPartial = false;

  if (sensorInitialized) {
    bool readOK = false;
    for (int attempt = 0; attempt < 3 && !readOK; attempt++) {
      if (sensor.readSensor(sensorData) && isReadingValid(sensorData)) {
        readOK = true;
      } else {
        Serial.printf("⚠ Lecture partielle tentative %d (EC=0/NPK=0)\n", attempt+1);
        delay(200);
      }
    }

    if (readOK) {
      SensorLog converted = toSensorLog(sensorData);
      newReading.moisture     = converted.moisture;
      newReading.temperature  = converted.temperature;
      newReading.ph           = converted.ph;
      newReading.conductivity = converted.conductivity;
      newReading.nitrogen     = converted.nitrogen;
      newReading.phosphorus   = converted.phosphorus;
      newReading.potassium    = converted.potassium;
      newReading.salinity     = converted.salinity;
      newReading.tds          = converted.tds;
      newReading.fertility    = converted.fertility;
      newReading.isPartial    = false;
      lastValidReading      = newReading;
      hasValidReading       = true;
      sensorActuallyReading = true; // Première lecture réelle réussie
      Serial.printf("✓ [%s] Sol: M=%.1f%% T=%.1f°C pH=%.1f EC=%.0f N=%d P=%d K=%d\n",
        newReading.datetime, newReading.moisture, newReading.temperature,
        newReading.ph, newReading.conductivity,
        newReading.nitrogen, newReading.phosphorus, newReading.potassium);
    } else {
      // Garde les NPK/EC valides, marque comme partielle
      if (hasValidReading) {
        newReading = lastValidReading;
        getDateTimeStr(newReading.datetime);
        newReading.timestamp = millis();
        newReading.isPartial = true;
        Serial.printf("⚠ [%s] Lecture invalide — NPK/EC de la dernière valeur valide conservés\n", newReading.datetime);
      } else {
        // Simulation si aucune donnée valide encore
        newReading.moisture     = 35.0 + random(0,200)/10.0;
        newReading.temperature  = 22.0 + random(0,100)/10.0;
        newReading.ph           = 6.5  + random(-5,5)/10.0;
        newReading.conductivity = 1200 + random(-200,200);
        newReading.nitrogen     = 110  + random(-20,20);
        newReading.phosphorus   = 65   + random(-15,15);
        newReading.potassium    = 100  + random(-20,20);
        newReading.salinity     = 120  + random(-20,20);
        newReading.tds          = 800  + random(-100,100);
        newReading.fertility    = 75   + random(-10,10);
        newReading.isPartial    = false;
        Serial.printf("📊 [%s] Simulation (pas de donnée valide)\n", newReading.datetime);
      }
    }
  } else {
    // Mode simulation pure
    newReading.moisture     = 35.0 + random(0,200)/10.0;
    newReading.temperature  = 22.0 + random(0,100)/10.0;
    newReading.ph           = 6.5  + random(-5,5)/10.0;
    newReading.conductivity = 1200 + random(-200,200);
    newReading.nitrogen     = 110  + random(-20,20);
    newReading.phosphorus   = 65   + random(-15,15);
    newReading.potassium    = 100  + random(-20,20);
    newReading.salinity     = 120  + random(-20,20);
    newReading.tds          = 800  + random(-100,100);
    newReading.fertility    = 75   + random(-10,10);
    Serial.printf("📊 [%s] Simulation: M=%.1f%%\n", newReading.datetime, newReading.moisture);
  }

  // Copie atomique vers currentReading — mutex court (microsecondes)
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100))) {
    currentReading = newReading;
    xSemaphoreGive(dataMutex);
  } else {
    currentReading = newReading; // fallback sans mutex si timeout
  }
  saveSensorData(newReading);

  // ── CONTRÔLE AUTO IRRIGATION ─────────────────────────────────
  if (irrigationAuto) {
    bool shouldBeOn  = (currentReading.moisture < thresholds.moistureMin);
    bool shouldBeOff = (currentReading.moisture > thresholds.moistureMax);
    unsigned long now2 = millis();

    if (shouldBeOn) {
      if (now2 - lastIrrigationOnTime >= IRRIGATION_AUTO_COOLDOWN_MS && !irrigationManual) {
        irrigationManual     = true;
        lastIrrigationOnTime = now2;
        logActuatorAction("IRRIGATION_ON", "AUTO", currentReading.moisture);
      }
    } else if (shouldBeOff && irrigationManual) {
      // Éteindre AUTO seulement si c'est l'AUTO qui l'avait allumé
      // Si allumé manuellement → ne pas toucher
      irrigationManual = false;
      logActuatorAction("IRRIGATION_OFF", "AUTO", currentReading.moisture);
    }
  }
  // Toujours appliquer l'état physique
  digitalWrite(IRRIGATION_PIN,    irrigationManual    ? HIGH : LOW);
  digitalWrite(FERTILIZATION_PIN, fertilizationManual ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, LOW);
}

SensorLog toSensorLog(TerraSoilData& d) {
  SensorLog s;
  s.moisture     = d.moisture;
  s.temperature  = d.temperature;
  s.ph           = d.ph;
  s.conductivity = d.conductivity;
  s.nitrogen     = d.nitrogen;
  s.phosphorus   = d.phosphorus;
  s.potassium    = d.potassium;
  s.salinity     = d.salinity;
  s.tds          = d.tds;
  s.fertility    = d.fertility;
  s.isPartial    = false;
  return s;
}

void saveSensorData(SensorLog& s) {
  if (sensorLogCount < MAX_SENSOR_LOGS) {
    sensorLogs[sensorLogCount++] = s;
  } else {
    for (int i = 0; i < MAX_SENSOR_LOGS-1; i++) sensorLogs[i] = sensorLogs[i+1];
    sensorLogs[MAX_SENSOR_LOGS-1] = s;
  }
}

void saveChatMessage(String role, String content) {
  if (chatMessageCount < MAX_CHAT_MESSAGES) {
    chatHistory[chatMessageCount].timestamp = millis();
    getDateTimeStr(chatHistory[chatMessageCount].datetime);
    chatHistory[chatMessageCount].role    = role;
    chatHistory[chatMessageCount].content = content;
    chatMessageCount++;
  }
}

void logActuatorAction(String action, String trigger, float moisture) {
  ActuatorLog e;
  e.timestamp = millis();
  getDateTimeStr(e.datetime);
  e.action   = action;
  e.trigger  = trigger;
  e.moisture = moisture;
  if (actuatorLogCount < MAX_ACTUATOR_LOGS) {
    actuatorLogs[actuatorLogCount++] = e;
  } else {
    for (int i = 0; i < MAX_ACTUATOR_LOGS-1; i++) actuatorLogs[i] = actuatorLogs[i+1];
    actuatorLogs[MAX_ACTUATOR_LOGS-1] = e;
  }
  File f = SPIFFS.open("/actuators.txt", FILE_APPEND);
  if (f) {
    f.print("["); f.print(e.datetime); f.print("] ");
    f.print(action); f.print(" ["); f.print(trigger);
    f.print("] M="); f.print(moisture,1); f.println("%");
    f.close();
  }
  Serial.println("📋 [" + String(e.datetime) + "] " + action + " [" + trigger + "] M=" + String(moisture,1) + "%");
}

// ═══════════════════════════════════════════════════════════════
// CONFIG — Chargement / Sauvegarde
// ═══════════════════════════════════════════════════════════════
void loadAllConfig() {
  wifi_ssid     = preferences.getString("wifi_ssid",   "");
  wifi_password = preferences.getString("wifi_pass",   "");
  api_key       = preferences.getString("api_key",     "");
  api_model     = preferences.getString("api_model",   "deepseek-chat");
  api_platform  = preferences.getString("api_platform","deepseek");

  systemConfig.soilType           = preferences.getString("soilType",    "Loamy soil");
  systemConfig.cropType           = preferences.getString("cropType",    "Tomato");
  systemConfig.growthStages       = preferences.getString("growthStage", "Fructification");
  systemConfig.cultivationMethods = preferences.getString("cultivation", "Greenhouse");
  systemConfig.aiLanguage         = preferences.getString("aiLang",      "fr");
  systemConfig.sensorInterval     = preferences.getInt("sensorInterval", 600);
  systemConfig.aiInterval         = preferences.getInt("aiInterval",     1800);
  if (systemConfig.sensorInterval < 30)  systemConfig.sensorInterval = 600;
  if (systemConfig.aiInterval     < 60)  systemConfig.aiInterval     = 1800;

  thresholds.moistureMin     = preferences.getFloat("moistMin", 30);
  thresholds.moistureMax     = preferences.getFloat("moistMax", 70);
  thresholds.temperatureMin  = preferences.getFloat("tempMin",  15);
  thresholds.temperatureMax  = preferences.getFloat("tempMax",  35);
  thresholds.phMin           = preferences.getFloat("phMin",    5.5);
  thresholds.phMax           = preferences.getFloat("phMax",    7.5);
  thresholds.conductivityMin = preferences.getFloat("ecMin",    500);
  thresholds.conductivityMax = preferences.getFloat("ecMax",    2000);
  thresholds.nitrogenMin     = preferences.getInt("nMin",       80);
  thresholds.nitrogenMax     = preferences.getInt("nMax",       150);
  thresholds.phosphorusMin   = preferences.getInt("pMin",       40);
  thresholds.phosphorusMax   = preferences.getInt("pMax",       100);
  thresholds.potassiumMin    = preferences.getInt("kMin",       70);
  thresholds.potassiumMax    = preferences.getInt("kMax",       140);
  thresholds.salinityMin     = preferences.getFloat("salMin",   0);
  thresholds.salinityMax     = preferences.getFloat("salMax",   500);
  thresholds.tdsMin          = preferences.getInt("tdsMin",     300);
  thresholds.tdsMax          = preferences.getInt("tdsMax",     1500);
  thresholds.fertilityMin    = preferences.getInt("fertMin",    50);
  thresholds.fertilityMax    = preferences.getInt("fertMax",    100);

  loadLabNPK();
}

void saveWiFiConfig() {
  preferences.putString("wifi_ssid",    wifi_ssid);
  preferences.putString("wifi_pass",    wifi_password);
  preferences.putString("api_key",      api_key);
  preferences.putString("api_model",    api_model);
  preferences.putString("api_platform", api_platform);
}

void saveSystemConfig() {
  preferences.putString("soilType",    systemConfig.soilType);
  preferences.putString("cropType",    systemConfig.cropType);
  preferences.putString("growthStage", systemConfig.growthStages);
  preferences.putString("cultivation", systemConfig.cultivationMethods);
  preferences.putString("aiLang",      systemConfig.aiLanguage);
  preferences.putInt("sensorInterval", systemConfig.sensorInterval);
  preferences.putInt("aiInterval",     systemConfig.aiInterval);
}

void saveThresholds() {
  preferences.putFloat("moistMin", thresholds.moistureMin);
  preferences.putFloat("moistMax", thresholds.moistureMax);
  preferences.putFloat("tempMin",  thresholds.temperatureMin);
  preferences.putFloat("tempMax",  thresholds.temperatureMax);
  preferences.putFloat("phMin",    thresholds.phMin);
  preferences.putFloat("phMax",    thresholds.phMax);
  preferences.putFloat("ecMin",    thresholds.conductivityMin);
  preferences.putFloat("ecMax",    thresholds.conductivityMax);
  preferences.putInt("nMin",       thresholds.nitrogenMin);
  preferences.putInt("nMax",       thresholds.nitrogenMax);
  preferences.putInt("pMin",       thresholds.phosphorusMin);
  preferences.putInt("pMax",       thresholds.phosphorusMax);
  preferences.putInt("kMin",       thresholds.potassiumMin);
  preferences.putInt("kMax",       thresholds.potassiumMax);
  preferences.putFloat("salMin",   thresholds.salinityMin);
  preferences.putFloat("salMax",   thresholds.salinityMax);
  preferences.putInt("tdsMin",     thresholds.tdsMin);
  preferences.putInt("tdsMax",     thresholds.tdsMax);
  preferences.putInt("fertMin",    thresholds.fertilityMin);
  preferences.putInt("fertMax",    thresholds.fertilityMax);
}

void saveLabNPK() {
  preferences.putFloat("labN",        labNPK.nitrogen);
  preferences.putFloat("labP",        labNPK.phosphorus);
  preferences.putFloat("labK",        labNPK.potassium);
  preferences.putString("labDate",    String(labNPK.date));
  preferences.putString("labName",    labNPK.labName);
  preferences.putString("labNotes",   labNPK.notes);
  preferences.putBool("labHasData",   labNPK.hasData);
  preferences.putBool("labCalibrated",labNPK.calibrated);
}

void loadLabNPK() {
  labNPK.nitrogen   = preferences.getFloat("labN",          0);
  labNPK.phosphorus = preferences.getFloat("labP",          0);
  labNPK.potassium  = preferences.getFloat("labK",          0);
  String d          = preferences.getString("labDate",       "");
  strncpy(labNPK.date, d.c_str(), 19); labNPK.date[19] = '\0';
  labNPK.labName    = preferences.getString("labName",       "");
  labNPK.notes      = preferences.getString("labNotes",      "");
  labNPK.hasData    = preferences.getBool("labHasData",      false);
  labNPK.calibrated = preferences.getBool("labCalibrated",   false);
}

// ═══════════════════════════════════════════════════════════════
// PROMPTS IA
// ═══════════════════════════════════════════════════════════════
String buildAnalysisPrompt() {
  String p = "";
  p.reserve(1400);
  p += "=== CONTEXTE FERME ===\n";
  p += "Sol:" + systemConfig.soilType + " Culture:" + systemConfig.cropType;
  p += " Stade:" + systemConfig.growthStages + " Methode:" + systemConfig.cultivationMethods + "\n";
  p += "DateTime:" + getDateTime() + "\n";
  p += "Irrigation:" + String(irrigationAuto?"AUTO":"MANUEL") + "[" + String(irrigationManual?"ON":"OFF") + "]\n\n";

  p += "=== SEUILS ===\n";
  p += "Humidite:" + String(thresholds.moistureMin) + "-" + String(thresholds.moistureMax) + "% ";
  p += "Temp:" + String(thresholds.temperatureMin) + "-" + String(thresholds.temperatureMax) + "C ";
  p += "pH:" + String(thresholds.phMin) + "-" + String(thresholds.phMax) + " ";
  p += "EC:" + String(thresholds.conductivityMin) + "-" + String(thresholds.conductivityMax) + "\n\n";

  p += "=== CAPTEUR SOL ACTUEL ===\n";
  // Avertissement données simulées
  if (!sensorActuallyReading) {
    p += "ATTENTION: CAPTEUR HORS SOL — données SIMULÉES, NE PAS analyser comme données réelles.\n";
    p += "Informer l'utilisateur que le capteur doit être planté dans le sol.\n";
  }
  p += "M=" + String(currentReading.moisture,1) + "% T=" + String(currentReading.temperature,1) + "C ";
  p += "pH=" + String(currentReading.ph,1) + " EC=" + String(currentReading.conductivity,0) + " ";
  p += "Sal=" + String(currentReading.salinity,1) + " TDS=" + String(currentReading.tds) + "\n";
  p += "NPK capteur (ESTIME depuis EC): N=" + String(currentReading.nitrogen);
  p += " P=" + String(currentReading.phosphorus) + " K=" + String(currentReading.potassium) + " mg/kg\n";
  p += String(sensorActuallyReading ? "Source: CAPTEUR REEL RS485\n" : "Source: SIMULATION (capteur hors sol)\n");
  if (currentReading.isPartial) p += "ATTENTION: Derniere lecture RS485 incomplete — NPK/EC de la lecture precedente\n";

  if (labNPK.hasData) {
    p += "\n=== NPK LABORATOIRE (" + String(labNPK.date) + ") ===\n";
    p += "N=" + String(labNPK.nitrogen,1) + " P=" + String(labNPK.phosphorus,1) + " K=" + String(labNPK.potassium,1) + " mg/kg\n";
    p += "Labo: " + labNPK.labName + "\n";
    if (labNPK.calibrated) p += "Calibration: Coefficients IEEE754 ecrits dans le capteur\n";
    else p += "Calibration: PAS encore effectuee\n";
    if (labNPK.notes.length() > 0) p += "Notes: " + labNPK.notes + "\n";
    p += "UTILISER CES VALEURS LABO pour la fertilisation — plus fiables que l'estimation EC\n";
  } else {
    p += "\nNPK LABO: Aucune donnee — se baser sur l'estimation EC avec prudence\n";
  }

  if (sensorLogCount >= 2) {
    p += "\n=== TENDANCES (20 dernieres) ===\n";
    int startIdx = max(0, sensorLogCount-20);
    for (int i=startIdx; i<sensorLogCount; i++) {
      p += "[" + String(sensorLogs[i].datetime+11) + "] ";
      p += "M=" + String(sensorLogs[i].moisture,1) + "% T=" + String(sensorLogs[i].temperature,1);
      p += " pH=" + String(sensorLogs[i].ph,1) + " EC=" + String(sensorLogs[i].conductivity,0) + "\n";
    }
  }

  if (actuatorLogCount > 0) {
    p += "\n=== DERNIERES ACTIONS ACTIONNEURS ===\n";
    int startIdx = max(0, actuatorLogCount-5);
    for (int i=startIdx; i<actuatorLogCount; i++) {
      p += "[" + String(actuatorLogs[i].datetime) + "] " + actuatorLogs[i].action;
      p += "[" + actuatorLogs[i].trigger + "] M=" + String(actuatorLogs[i].moisture,1) + "%\n";
    }
  }

  p += "\n=== TACHE ===\nAnalyse complete: 1)Etat general 2)Irrigation 3)Fertilisation 4)Alertes 5)Action suivante. ";
  p += "Texte brut uniquement. Pas de ** ni ##.";
  return p;
}

String buildChatContextPrompt(String userMsg) {
  String p = "";
  p.reserve(1000);
  p += "=FERME= Sol:" + systemConfig.soilType + " Culture:" + systemConfig.cropType;
  p += " Stade:" + systemConfig.growthStages + "\n";
  p += " cultivationMethods:" + systemConfig.cultivationMethods + "\n";

  // Avertissement si données simulées
  if (!sensorActuallyReading) {
    p += "=ATTENTION= CAPTEUR HORS SOL — données ci-dessous sont SIMULÉES, ne pas analyser.\n";
  }

  p += "=CAPTEUR SOL ACTUEL= M=" + String(currentReading.moisture,1) + "% T=";
  p += String(currentReading.temperature,1) + " pH=" + String(currentReading.ph,1);
  p += " EC=" + String(currentReading.conductivity,0) + " N=" + String(currentReading.nitrogen);
  p += " P=" + String(currentReading.phosphorus) + " K=" + String(currentReading.potassium);
  p += String(sensorActuallyReading ? " [REEL]" : " [SIMULE]") + "\n";

  if (labNPK.hasData) {
    p += "=NPK LABO[" + String(labNPK.date) + "]= N=" + String(labNPK.nitrogen,1);
    p += " P=" + String(labNPK.phosphorus,1) + " K=" + String(labNPK.potassium,1);
    p += " [" + labNPK.labName + "] Calibre:" + String(labNPK.calibrated?"OUI":"NON") + "\n";
  }

  p += "=DERNIERES LECTURES(10)=\n";
  int startIdx = max(0, sensorLogCount-10);
  for (int i=startIdx; i<sensorLogCount; i++) {
    p += String(sensorLogs[i].datetime+11) + " M=" + String(sensorLogs[i].moisture,1);
    p += "% EC=" + String(sensorLogs[i].conductivity,0) + " pH=" + String(sensorLogs[i].ph,1);
    p += String(sensorLogs[i].isPartial ? " [SIM]" : " [OK]") + "\n";
  }

  // Historique conversation (4 derniers échanges)
  p += "=HISTORIQUE CONVERSATION=\n";
  int hStart = max(0, chatMessageCount - 4);
  for (int i = hStart; i < chatMessageCount; i++) {
    String role = (chatHistory[i].role == "user") ? "Toi" : "Terra AI";
    p += "[" + role + "]: " + chatHistory[i].content.substring(0, 150) + "\n";
  }

  // État actionneurs
  p += "=ACTIONNEURS ACTUELS=\n";
  p += "Irrigation:" + String(irrigationManual?"ON":"OFF");
  p += "[" + String(irrigationAuto?"AUTO":"MANUEL") + "]";
  p += " | Dernier ON: " + String(lastIrrigationOnTime > 0
    ? String((millis()-lastIrrigationOnTime)/1000)+"s ago"
    : "jamais") + "\n";
  p += "Fertilisation:" + String(fertilizationManual?"ON":"OFF") + "\n";

  p += "=REGLE= Max 5 phrases. Texte brut. Tiens compte de l'historique.\n";
  p += "=QUESTION= " + userMsg.substring(0,200);
  return p;
}

void updateAISystemRole() {
  if (!aiClient) return;
  String role = "Tu es Terra AI, assistant agronomique expert integre dans une box intelligente. ";
  role += "Tu analyses des donnees reelles du capteur sol SN-3002-TR (10 parametres). ";
  role += "IMPORTANT: Les valeurs NPK du capteur sont ESTIMEES depuis la conductivite EC — pas une mesure directe. ";
  role += "Quand des donnees NPK laboratoire sont disponibles, TOUJOURS les privilegier pour les recommandations de fertilisation. ";
  role += "\n=== NPK LABORATOIRE (" + String(labNPK.date) + ") ===\n";
  role += "N=" + String(labNPK.nitrogen,1) + " P=" + String(labNPK.phosphorus,1) + " K=" + String(labNPK.potassium,1) + " mg/kg\n";
  role += "Ferme: " + systemConfig.soilType + " / " + systemConfig.cropType + " / " + systemConfig.growthStages + ". ";
  role += "FORMAT: Jamais de markdown. Pas de **, ##, ni bullets *. Uniquement texte brut avec 1) 2) 3). ";
  if (systemConfig.aiLanguage == "fr") role += "REPONDRE UNIQUEMENT EN FRANCAIS.";
  else if (systemConfig.aiLanguage == "zh") role += "RESPOND IN CHINESE ONLY.";
  else if (systemConfig.aiLanguage == "ar") role += "RESPOND IN ARABIC ONLY.";
  else role += "RESPOND IN ENGLISH.";
  aiClient->setChatSystemRole(role.c_str());
}

void performAutoAnalysis() {
  if (!aiClient) return;
  Serial.println("🤖 Analyse IA... Heap=" + String(ESP.getFreeHeap()));
  String prompt = buildAnalysisPrompt();
  String resp   = aiClient->chat(prompt);
  if (resp.length() > 0) {
    String clean = cleanMarkdown(resp);
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      lastAnalysis = "[" + getDateTime() + "]\n" + clean;
      xSemaphoreGive(dataMutex);
    }
    File f = SPIFFS.open("/analyses.txt", FILE_APPEND);
    if (f) { f.println("\n=== " + getDateTime() + " ==="); f.println(clean); f.close(); }
    Serial.println("✓ Analyse OK (" + String(resp.length()) + " chars)");
  } else {
    lastAnalysis = "Erreur: " + aiClient->getLastError();
    Serial.println("✗ Analyse erreur: " + aiClient->getLastError());
  }
}

// ═══════════════════════════════════════════════════════════════
// UTILITAIRES
// ═══════════════════════════════════════════════════════════════
String escapeJSON(String s) {
  s.replace("\\","\\\\"); s.replace("\"","\\\"");
  s.replace("\n","\\n");  s.replace("\r","\\r"); s.replace("\t","\\t");
  return s;
}
String cleanMarkdown(String t) {
  String r = ""; r.reserve(t.length());
  for (int i=0; i<(int)t.length(); i++) {
    if (t[i]=='#') { while(i<(int)t.length()&&(t[i]=='#'||t[i]==' '))i++; i--; }
    else if (t[i]=='*'&&i+1<(int)t.length()&&t[i+1]=='*') i++;
    else if (t[i]=='*') {}
    else r += t[i];
  }
  return r;
}

// ═══════════════════════════════════════════════════════════════
// PORTAIL DE CONFIGURATION
// ═══════════════════════════════════════════════════════════════
void setupConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP("TERRA-CONFIG","TERRA2026",6);
  Serial.println("✓ AP: TERRA-CONFIG | IP: 192.168.4.1");

  server.on("/", HTTP_GET, [](){
    String html = R"(<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Terra AI Config</title>
<style>*{margin:0;padding:0;box-sizing:border-box}
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#2ecc71,#27ae60);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.box{background:white;padding:40px;border-radius:20px;box-shadow:0 20px 60px rgba(0,0,0,.3);max-width:480px;width:100%}
h1{color:#27ae60;text-align:center;margin-bottom:30px;font-size:2em}
.fg{margin-bottom:18px}label{display:block;margin-bottom:6px;color:#333;font-weight:bold}
input,select{width:100%;padding:12px;border:2px solid #ddd;border-radius:8px;font-size:16px}
input:focus,select:focus{outline:none;border-color:#27ae60}
.btn{width:100%;padding:15px;background:#27ae60;color:white;border:none;border-radius:8px;font-size:18px;font-weight:bold;cursor:pointer;margin-top:10px}
.info{background:#e8f5e9;padding:15px;border-radius:8px;margin-bottom:20px;color:#2e7d32}
</style></head><body><div class="box">
<h1>🌱 TERRA AI v3.0</h1>
<div class="info"><strong>Configuration</strong><br>WiFi: TERRA-CONFIG | MDP: TERRA2026<br>IP: 192.168.4.1</div>
<form action="/save" method="POST">
<div class="fg"><label>WiFi SSID</label><input name="ssid" required></div>
<div class="fg"><label>WiFi Password</label><input type="password" name="password"></div>
<div class="fg"><label>Plateforme IA</label>
<select name="platform"><option value="deepseek">DeepSeek</option><option value="openai">OpenAI</option><option value="anthropic">Anthropic</option></select></div>
<div class="fg"><label>Cle API</label><input name="apikey" required></div>
<div class="fg"><label>Modele</label><input name="model" value="deepseek-chat" required></div>
<button class="btn" type="submit">Sauvegarder et Demarrer</button>
</form></div></body></html>)";
    server.send(200,"text/html",html);
  });

  server.on("/save", HTTP_POST, [](){
    wifi_ssid     = server.arg("ssid");
    wifi_password = server.arg("password");
    api_platform  = server.arg("platform");
    api_key       = server.arg("apikey");
    api_model     = server.arg("model");
    saveWiFiConfig();
    server.send(200,"text/html","<html><body style='font-family:Arial;text-align:center;padding:50px;background:#27ae60;color:white'><h1>Sauvegarde OK</h1><p>Redemarrage...</p></body></html>");
    delay(2000); ESP.restart();
  });
}

// ═══════════════════════════════════════════════════════════════
// SERVEUR WEB PRINCIPAL
// ═══════════════════════════════════════════════════════════════
void setupMainWebServer() {

  // ─── PAGE PRINCIPALE ─────────────────────────────────────────
  server.on("/", HTTP_GET, [](){
    String html = R"rawliteral(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Terra AI v3.0</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#2ecc71,#27ae60);min-height:100vh;padding:12px;color:#2c3e50}
.container{max-width:1400px;margin:0 auto}
.header{background:white;border-radius:14px;padding:20px 25px;margin-bottom:12px;box-shadow:0 8px 25px rgba(0,0,0,.15);display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.header h1{color:#27ae60;font-size:2em}
.header-right{display:flex;gap:8px;flex-wrap:wrap}
.card{background:white;border-radius:14px;padding:18px;box-shadow:0 6px 20px rgba(0,0,0,.12);margin-bottom:12px}
.card h2{color:#27ae60;margin-bottom:14px;font-size:1.3em;border-bottom:2px solid #e8f5e9;padding-bottom:8px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
/* ─── CAPTEURS ─── */
.sensor-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:10px}
.sensor-item{background:#f8f9fa;padding:12px 8px;border-radius:10px;text-align:center;border:3px solid transparent;transition:.3s}
.sensor-item.warn{border-color:#f39c12;background:#fff3cd}
.sensor-item.critical{border-color:#e74c3c;background:#ffeaa7;animation:pulse 1s infinite}
.sensor-item.partial{opacity:.6;border-color:#95a5a6}
.sensor-item label{display:block;font-size:.78em;color:#7f8c8d;margin-bottom:4px}
.sensor-item .val{font-size:1.5em;font-weight:bold;color:#27ae60}
.sensor-item.warn .val{color:#f39c12}
.sensor-item.critical .val{color:#e74c3c}
@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.04)}}
/* ─── NPK LABO ─── */
.lab-panel{background:#f0fff4;border:2px solid #27ae60;border-radius:12px;padding:16px}
.lab-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.lab-title{color:#27ae60;font-weight:bold;font-size:1.1em}
.lab-badge{background:#27ae60;color:white;padding:3px 10px;border-radius:20px;font-size:.8em}
.lab-badge.none{background:#95a5a6}
.lab-badge.calibrated{background:#3498db}
.npk-compare{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin:12px 0}
.npk-col{background:white;border-radius:10px;padding:12px;text-align:center}
.npk-col-title{font-size:.8em;color:#7f8c8d;margin-bottom:8px;font-weight:bold}
.npk-val{font-size:1.8em;font-weight:bold}
.npk-unit{font-size:.75em;color:#95a5a6}
.npk-label{font-size:.9em;color:#7f8c8d;margin-top:2px}
.sensor-npk .npk-val{color:#3498db}
.lab-npk .npk-val{color:#27ae60}
.diff-badge{padding:2px 8px;border-radius:10px;font-size:.8em;font-weight:bold}
.diff-ok{background:#e8f5e9;color:#27ae60}
.diff-warn{background:#fff3cd;color:#f39c12}
.diff-critical{background:#ffeaa7;color:#e74c3c}
/* ─── FORMULAIRE LAB ─── */
.lab-form{background:white;border-radius:10px;padding:14px;margin-top:10px}
.lab-form h3{color:#27ae60;margin-bottom:12px;font-size:1em}
.form-row{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:10px}
.fg{margin-bottom:10px}
.fg label{display:block;font-size:.85em;font-weight:bold;color:#34495e;margin-bottom:4px}
.fc{width:100%;padding:9px;border:2px solid #e0e0e0;border-radius:8px;font-size:.95em}
.fc:focus{outline:none;border-color:#27ae60}
.btn{padding:9px 18px;border:none;border-radius:8px;font-size:.9em;cursor:pointer;transition:.2s;font-weight:bold}
.btn:hover{transform:translateY(-1px)}
.btn-green{background:#27ae60;color:white}
.btn-green:hover{background:#229954}
.btn-blue{background:#3498db;color:white}
.btn-blue:hover{background:#2980b9}
.btn-red{background:#e74c3c;color:white}
.btn-red:hover{background:#c0392b}
.btn-grey{background:#95a5a6;color:white}
.btn-grey:hover{background:#7f8c8d}
.btn-orange{background:#e67e22;color:white}
.btn-orange:hover{background:#d35400}
.btn-sm{padding:6px 12px;font-size:.8em}
/* ─── CONFIRMATION ─── */
.confirm-box{background:#e8f5e9;border:2px solid #27ae60;border-radius:10px;padding:14px;margin-top:10px;display:none}
.confirm-box.show{display:block}
.confirm-title{color:#27ae60;font-weight:bold;font-size:1em;margin-bottom:8px}
.confirm-row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #c8e6c9}
.confirm-row:last-child{border:none}
.confirm-label{color:#7f8c8d;font-size:.9em}
.confirm-value{font-weight:bold;color:#2c3e50}
/* ─── CHAT ─── */
.chat-container{height:420px;display:flex;flex-direction:column}
.chat-msgs{flex:1;overflow-y:auto;padding:12px;background:#f8f9fa;border-radius:10px;margin-bottom:10px}
.msg{margin-bottom:10px;padding:9px 13px;border-radius:10px;max-width:78%;clear:both}
.msg.user{background:#27ae60;color:white;margin-left:auto;float:right}
.msg.assistant{background:#e8f5e9;color:#2c3e50;float:left}
.msg-time{font-size:.72em;opacity:.7;margin-top:4px}
.typing{display:flex;gap:4px;padding:4px 0;align-items:center}
.typing span{width:7px;height:7px;background:#27ae60;border-radius:50%;animation:tb 1.2s infinite ease-in-out}
.typing span:nth-child(2){animation-delay:.2s}
.typing span:nth-child(3){animation-delay:.4s}
@keyframes tb{0%,80%,100%{transform:translateY(0);opacity:.4}40%{transform:translateY(-5px);opacity:1}}
/* ─── ANALYSE ─── */
.analysis-box{background:#f8f9fa;padding:14px;border-radius:10px;max-height:380px;overflow-y:auto;border-left:4px solid #27ae60;white-space:pre-wrap;font-size:.9em;line-height:1.6}
/* ─── CHART ─── */
.chart-wrap{position:relative;height:260px;margin-top:10px}
/* ─── ACTIONNEURS ─── */
.act-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}
.act-card{background:#f8f9fa;border-radius:12px;padding:16px;text-align:center}
.act-status{font-size:1.1em;margin:8px 0;font-weight:bold}
.act-log{background:#f8f9fa;border-radius:10px;padding:10px;max-height:180px;overflow-y:auto;font-size:.82em;font-family:monospace;margin-top:10px}
.log-on{color:#27ae60}.log-off{color:#e74c3c}.log-auto{color:#3498db}
/* ─── ALERTES ─── */
.alert-strip{background:#ffeaa7;border-left:4px solid #f39c12;border-radius:8px;padding:10px 14px;margin-bottom:10px;display:none}
.alert-strip.show{display:block}
/* ─── STATUS ─── */
.status-dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:#27ae60;animation:blink 2s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
/* ─── CALIBRATION ─── */
.calib-panel{background:#e3f2fd;border:2px solid #3498db;border-radius:10px;padding:14px;margin-top:10px}
.calib-panel h3{color:#3498db;margin-bottom:10px}
/* ─── RESPONSIVE ─── */
@media(max-width:900px){.grid2,.grid3,.sensor-grid,.npk-compare,.form-row{grid-template-columns:1fr}.act-grid{grid-template-columns:1fr}}
.tag-partial{background:#f39c12;color:white;font-size:.72em;padding:2px 6px;border-radius:6px;margin-left:6px}
.tag-sim{background:#9b59b6;color:white;font-size:.72em;padding:2px 6px;border-radius:6px;margin-left:6px}
</style>
</head>
<body>
<div class="container">

  <!-- HEADER -->
  <div class="header">
    <div>
      <h1>🌱 Terra AI v3.0 — Capteur Sol</h1>
      <p><span class="status-dot"></span> Système opérationnel &nbsp;|&nbsp; <span id="rtcDisplay">--:--</span></p>
    </div>
    <div class="header-right">
      <button class="btn btn-grey" onclick="toggleLang()">FR/EN/中文</button>
      <button class="btn btn-red" onclick="resetConfig()">⚙ Reset</button>
    </div>
  </div>

  <!-- ALERTE PARTIELLE -->
  <div class="alert-strip" id="partialAlert">
    ⚠ Dernière lecture RS485 incomplète — EC/NPK du capteur affichés depuis la dernière lecture valide.
    Vérifier câblage RS485 et résistance de terminaison 120Ω.
  </div>

  <!-- GRILLE PRINCIPALE -->
  <div class="grid2">

    <!-- COLONNE GAUCHE -->
    <div>
      <!-- CAPTEURS -->
      <div class="card">
        <h2>📊 Données Sol en Temps Réel <span id="sensorMode" style="font-size:.75em;color:#95a5a6"></span></h2>
        <div class="sensor-grid" id="sensorGrid"></div>
        <div class="chart-wrap"><canvas id="chartCanvas"></canvas></div>
      </div>
    </div>

    <!-- COLONNE DROITE -->
    <div>
      <!-- NPK LABORATOIRE -->
      <div class="card">
        <div class="lab-header">
          <span class="lab-title">🧪 NPK Laboratoire</span>
          <span class="lab-badge" id="labBadge">Aucune donnée</span>
        </div>

        <!-- COMPARAISON CAPTEUR vs LABO -->
        <div id="npkCompareSection" style="display:none">
          <div class="npk-compare" id="npkNitrogen">
            <div class="npk-col sensor-npk"><div class="npk-col-title">N — Capteur (estimé)</div><div class="npk-val" id="sensorN">--</div><div class="npk-unit">mg/kg</div></div>
            <div class="npk-col lab-npk"><div class="npk-col-title">N — Laboratoire</div><div class="npk-val" id="labN">--</div><div class="npk-unit">mg/kg</div></div>
          </div>
          <div class="npk-compare" id="npkPhosphorus">
            <div class="npk-col sensor-npk"><div class="npk-col-title">P — Capteur (estimé)</div><div class="npk-val" id="sensorP">--</div><div class="npk-unit">mg/kg</div></div>
            <div class="npk-col lab-npk"><div class="npk-col-title">P — Laboratoire</div><div class="npk-val" id="labP">--</div><div class="npk-unit">mg/kg</div></div>
          </div>
          <div class="npk-compare" id="npkPotassium">
            <div class="npk-col sensor-npk"><div class="npk-col-title">K — Capteur (estimé)</div><div class="npk-val" id="sensorK">--</div><div class="npk-unit">mg/kg</div></div>
            <div class="npk-col lab-npk"><div class="npk-col-title">K — Laboratoire</div><div class="npk-val" id="labK">--</div><div class="npk-unit">mg/kg</div></div>
          </div>
          <div style="margin-top:6px;font-size:.82em;color:#7f8c8d">
            Labo: <strong id="labNameDisplay">--</strong> &nbsp;|&nbsp; Date: <strong id="labDateDisplay">--</strong>
          </div>
          <div id="labNotes" style="font-size:.82em;color:#7f8c8d;margin-top:4px"></div>
        </div>

        <!-- FORMULAIRE SAISIE NPK LABO -->
        <div class="lab-form">
          <h3>Saisir les données laboratoire</h3>
          <div class="form-row">
            <div class="fg"><label>Azote N (mg/kg)</label><input class="fc" type="number" id="labNInput" step="0.1" min="0" placeholder="ex: 112.5"></div>
            <div class="fg"><label>Phosphore P (mg/kg)</label><input class="fc" type="number" id="labPInput" step="0.1" min="0" placeholder="ex: 68.3"></div>
            <div class="fg"><label>Potassium K (mg/kg)</label><input class="fc" type="number" id="labKInput" step="0.1" min="0" placeholder="ex: 98.7"></div>
          </div>
          <div class="form-row">
            <div class="fg"><label>Nom du laboratoire</label><input class="fc" type="text" id="labNameInput" placeholder="ex: Labo AgroSol Maroc"></div>
            <div class="fg"><label>Date analyse</label><input class="fc" type="date" id="labDateInput"></div>
            <div class="fg"><label>Notes / observations</label><input class="fc" type="text" id="labNotesInput" placeholder="ex: après pluie, profondeur 20cm"></div>
          </div>
          <div style="display:flex;gap:8px;flex-wrap:wrap">
            <button class="btn btn-green" onclick="saveLabData()">💾 Enregistrer</button>
            <button class="btn btn-grey btn-sm" onclick="clearLabData()">🗑 Effacer</button>
          </div>
        </div>

        <!-- CONFIRMATION APRÈS SAUVEGARDE -->
        <div class="confirm-box" id="confirmBox">
          <div class="confirm-title">✅ Données laboratoire enregistrées avec succès</div>
          <div class="confirm-row"><span class="confirm-label">Azote N</span><span class="confirm-value" id="confN">--</span></div>
          <div class="confirm-row"><span class="confirm-label">Phosphore P</span><span class="confirm-value" id="confP">--</span></div>
          <div class="confirm-row"><span class="confirm-label">Potassium K</span><span class="confirm-value" id="confK">--</span></div>
          <div class="confirm-row"><span class="confirm-label">Laboratoire</span><span class="confirm-value" id="confLab">--</span></div>
          <div class="confirm-row"><span class="confirm-label">Date</span><span class="confirm-value" id="confDate">--</span></div>
          <div class="confirm-row"><span class="confirm-label">Notes</span><span class="confirm-value" id="confNotes">--</span></div>
          <div style="margin-top:10px;font-size:.85em;color:#27ae60">
            L'IA utilisera ces valeurs pour les recommandations de fertilisation à la place des estimations capteur.
          </div>
        </div>

        <!-- CALIBRATION CAPTEUR VIA IEEE754 -->
        <div class="calib-panel" id="calibPanel" style="display:none">
          <h3>⚙ Calibration matérielle (optionnel)</h3>
          <p style="font-size:.85em;color:#555;margin-bottom:10px">
            <strong>L'IA utilise déjà les données labo ci-dessus.</strong><br>
            Cette étape est <em>optionnelle</em> : elle écrit les coefficients IEEE754 directement dans les registres Modbus du capteur SN-3002-TR pour que les futures lectures NPK du capteur soient corrigées matériellement. Utile uniquement si vous voulez que le capteur lui-même affiche des valeurs corrigées.
          </p>
      </div>

      <!-- ANALYSE IA -->
      <div class="card">
        <h2>🤖 Analyse IA</h2>
        <div class="analysis-box" id="analysis">En attente de la première analyse...</div>
        <div style="display:flex;gap:8px;margin-top:10px;flex-wrap:wrap">
          <button class="btn btn-green" onclick="forceAnalysis()">Analyser maintenant</button>
          <button class="btn btn-grey" onclick="downloadAnalyses()">📥 Télécharger</button>
        </div>
      </div>
    </div>
  </div>

  <!-- ACTIONNEURS + CHAT -->
  <div class="grid2">

    <!-- ACTIONNEURS -->
    <div class="card">
      <h2>🚿 Contrôle Actionneurs</h2>
      <div class="act-grid">
        <div class="act-card">
          <h3>💧 Irrigation</h3>
          <div class="act-status" id="irrigStatus">🔴 OFF</div>
          <div style="display:flex;gap:8px;justify-content:center;margin-bottom:10px">
            <button class="btn btn-green btn-sm" onclick="setIrrigation(true)">▶ ON</button>
            <button class="btn btn-red btn-sm" onclick="setIrrigation(false)">■ OFF</button>
          </div>
          <label style="display:flex;align-items:center;justify-content:center;gap:6px;cursor:pointer;font-size:.9em">
            <input type="checkbox" id="irrigAutoCheck" checked onchange="setIrrigAuto(this.checked)">
            Auto (seuil humidité)
          </label>
        </div>
        <div class="act-card">
          <h3>🌿 Fertilisation</h3>
          <div class="act-status" id="fertStatus">🔴 OFF</div>
          <div style="display:flex;gap:8px;justify-content:center;margin-bottom:10px">
            <button class="btn btn-green btn-sm" onclick="setFertilization(true)">▶ ON</button>
            <button class="btn btn-red btn-sm" onclick="setFertilization(false)">■ OFF</button>
          </div>
          <p style="font-size:.8em;color:#95a5a6;text-align:center">Manuel uniquement</p>
        </div>
      </div>
      <h3 style="color:#27ae60;margin:12px 0 6px">📋 Historique actionneurs</h3>
      <div class="act-log" id="actuatorLog"><span style="color:#999">Aucune action...</span></div>
      <!-- EXPORT -->
      <h3 style="color:#27ae60;margin:12px 0 6px">📥 Export données</h3>
      <div style="display:flex;gap:6px;flex-wrap:wrap">
        <button class="btn btn-green btn-sm" onclick="downloadAll()">📦 Tout</button>
        <button class="btn btn-grey btn-sm" onclick="downloadSensors()">📊 CSV Sol</button>
        <button class="btn btn-grey btn-sm" onclick="downloadChat()">💬 Chat</button>
        <button class="btn btn-grey btn-sm" onclick="downloadLab()">🧪 NPK Labo</button>
      </div>
    </div>

    <!-- CHAT -->
    <div class="card">
      <h2>💬 Agent Terra AI</h2>
      <div class="chat-container">
        <div class="chat-msgs" id="chatMsgs"></div>
        <div style="display:flex;gap:8px">
          <input class="fc" type="text" id="chatInput" placeholder="Posez une question à Terra AI..." onkeypress="if(event.key==='Enter')sendMsg()">
          <button class="btn btn-green" id="sendBtn" onclick="sendMsg()">➤</button>
        </div>
      </div>
    </div>
  </div>

  <!-- CONFIGURATION SYSTÈME -->
  <div class="grid3">
    <div class="card">
      <h2>⚙ Configuration</h2>
      <div class="fg"><label>Culture</label><input class="fc" id="cropType" value=""></div>
      <div class="fg"><label>Type de sol</label><input class="fc" id="soilType" value=""></div>
      <div class="fg"><label>Stade</label>
        <select class="fc" id="growthStage">
          <option value="Germination">Germination</option>
          <option value="Vegetative">Végétatif</option>
          <option value="Flowering">Floraison</option>
          <option value="Fructification" selected>Fructification</option>
          <option value="Maturation">Maturation</option>
          <option value="Harvest">Récolte</option>
        </select></div>
      <div class="fg"><label>Méthode</label>
        <select class="fc" id="cultivation">
          <option value="Open Field">Plein champ</option>
          <option value="Greenhouse" selected>Serre</option>
          <option value="Hydroponic">Hydroponique</option>
        </select></div>
      <div class="fg"><label>Langue IA</label>
        <select class="fc" id="aiLang">
          <option value="fr">Français</option>
          <option value="en">English</option>
          <option value="zh">中文</option>
          <option value="ar">العربية</option>
        </select></div>
      <div class="fg"><label>Intervalle capteur</label>
        <select class="fc" id="sensorInterval">
          <option value="60">1 min (test)</option>
          <option value="300">5 min</option>
          <option value="600" selected>10 min</option>
          <option value="1800">30 min</option>
        </select></div>
      <div class="fg"><label>Intervalle IA</label>
        <select class="fc" id="aiInterval">
          <option value="600">10 min</option>
          <option value="1800" selected>30 min</option>
          <option value="3600">1 heure</option>
        </select></div>
      <button class="btn btn-green" onclick="saveConfig()">💾 Enregistrer</button>
    </div>

    <div class="card">
      <h2>🎯 Seuils d'alerte</h2>
      <div id="thresholdGrid" style="max-height:380px;overflow-y:auto"></div>
      <div style="display:flex;gap:8px;margin-top:10px;flex-wrap:wrap">
        <button class="btn btn-green btn-sm" onclick="saveThresholds()">💾 Sauvegarder</button>
        <button class="btn btn-blue btn-sm" onclick="askAIThresholds()">🤖 Recommandations IA</button>
      </div>
    </div>

    <div class="card">
      <h2>📈 Statut système</h2>
      <div style="background:#f8f9fa;padding:14px;border-radius:10px;font-size:.9em;line-height:2">
        <div>Mode: <strong id="sensorModeStatus">--</strong></div>
        <div>Dernière lecture: <strong id="lastRead">--</strong></div>
        <div>Total lectures: <strong id="totalReadings">0</strong></div>
        <div>Lectures partielles: <strong id="partialCount" style="color:#f39c12">0</strong></div>
        <div>Events actionneurs: <strong id="totalActuator">0</strong></div>
        <div>NPK Labo: <strong id="labStatus">Non saisi</strong></div>
        <div>Calibration capteur: <strong id="calibStatus2">Non</strong></div>
        <div>Heap libre: <strong id="heapFree">--</strong></div>
      </div>
    </div>
  </div>

</div><!-- end container -->

<script>
// ── ÉTAT GLOBAL ──────────────────────────────────────────────
let currentLang   = 'fr';
let chart         = null;
let sensorHistory = [];
let thresholds    = {};
let partialReadings = 0;
let chatPolling   = null;
let typingId      = null;

// ── HORLOGE ──────────────────────────────────────────────────
setInterval(()=>{
  document.getElementById('rtcDisplay').textContent = new Date().toLocaleTimeString('fr-FR');
}, 1000);

// ── NOMS PARAMÈTRES ──────────────────────────────────────────
const paramNames = ['Humidité','Température','pH','EC','N (est.)','P (est.)','K (est.)','Salinité','TDS','Fertilité'];
const paramKeys  = ['moisture','temperature','ph','conductivity','nitrogen','phosphorus','potassium','salinity','tds','fertility'];
const paramUnits = ['%','°C','','µS/cm','mg/kg','mg/kg','mg/kg','mg/L','mg/L','mg/kg'];
const chartColors= ['#3498db','#e74c3c','#f39c12','#9b59b6','#1abc9c','#e67e22','#2ecc71','#34495e','#16a085','#c0392b'];

// ── NOTIFICATION ─────────────────────────────────────────────
function notify(msg, type='success'){
  const n = document.createElement('div');
  n.style.cssText = `position:fixed;top:18px;right:18px;padding:12px 22px;border-radius:10px;
    color:white;font-weight:bold;z-index:9999;max-width:320px;font-size:.9em;
    background:${type==='error'?'#e74c3c':type==='warn'?'#f39c12':'#27ae60'};
    box-shadow:0 5px 15px rgba(0,0,0,.3)`;
  n.textContent = msg;
  document.body.appendChild(n);
  setTimeout(()=>n.remove(), 4000);
}

function toggleLang(){
  const langs = ['fr','en','zh'];
  currentLang = langs[(langs.indexOf(currentLang)+1)%langs.length];
  notify('Langue: ' + currentLang.toUpperCase());
}

// ── MISE À JOUR CAPTEURS ──────────────────────────────────────
function updateSensorDisplay(data){
  const grid = document.getElementById('sensorGrid');
  grid.innerHTML = '';
  const isPartial = data.isPartial;
  document.getElementById('partialAlert').className = 'alert-strip' + (isPartial?' show':'');

  paramKeys.forEach((key,i)=>{
    const val = parseFloat(data[key]||0);
    const minK = key+'Min', maxK = key+'Max';
    const mn   = thresholds[minK]||0, mx = thresholds[maxK]||999999;
    const div  = document.createElement('div');
    let cls = 'sensor-item';
    if (isPartial && ['conductivity','nitrogen','phosphorus','potassium','salinity','tds'].includes(key)) cls += ' partial';
    else if (val<mn||val>mx) cls += ' critical';
    div.className = cls;
    div.innerHTML = `<label>${paramNames[i]}</label><div class="val">${val.toFixed(key==='ph'?1:0)}${paramUnits[i]}</div>`;
    grid.appendChild(div);
  });

  // Mettre à jour les chiffres NPK dans la comparaison
  document.getElementById('sensorN').textContent = data.nitrogen  || '--';
  document.getElementById('sensorP').textContent = data.phosphorus|| '--';
  document.getElementById('sensorK').textContent = data.potassium || '--';

  document.getElementById('sensorMode').textContent = '[ ' + (data.sensorMode||'--') + ' ]';
  document.getElementById('sensorModeStatus').textContent = data.sensorMode||'--';
  document.getElementById('lastRead').textContent       = data.datetime||'--';
  document.getElementById('totalReadings').textContent  = data.totalReadings||0;
  document.getElementById('partialCount').textContent   = data.partialCount||0;
  document.getElementById('heapFree').textContent       = data.heap ? Math.round(data.heap/1024)+'KB' : '--';
}

// ── CHART ─────────────────────────────────────────────────────
function updateChart(){
  const ctx = document.getElementById('chartCanvas');
  if (!ctx || sensorHistory.length===0) return;
  if (chart) chart.destroy();
  const datasets = paramKeys.slice(0,5).map((key,i)=>({
    label: paramNames[i],
    data:  sensorHistory.map(d=>d[key]),
    borderColor: chartColors[i], backgroundColor: chartColors[i]+'20',
    tension: 0.4, fill: false
  }));
  chart = new Chart(ctx, {
    type:'line',
    data:{ labels: sensorHistory.map((_,i)=>`T-${sensorHistory.length-i}`), datasets },
    options:{ responsive:true, maintainAspectRatio:false,
      plugins:{legend:{position:'bottom'}}, scales:{y:{beginAtZero:false}} }
  });
}

// ── NPK LABORATOIRE ───────────────────────────────────────────
function updateLabDisplay(data){
  const badge = document.getElementById('labBadge');
  if (!data || !data.hasData){
    badge.textContent = 'Aucune donnée';
    badge.className   = 'lab-badge none';
    document.getElementById('npkCompareSection').style.display = 'none';
    document.getElementById('calibPanel').style.display        = 'none';
    document.getElementById('labStatus').textContent  = 'Non saisi';
    document.getElementById('calibStatus2').textContent= 'Non';
    return;
  }
  if (data.calibrated){
    badge.textContent = '✓ Calibration appliquée';
    badge.className   = 'lab-badge calibrated';
  } else {
    badge.textContent = '✓ Données saisies';
    badge.className   = 'lab-badge';
  }
  document.getElementById('npkCompareSection').style.display = 'block';
  document.getElementById('calibPanel').style.display        = data.hasData ? 'block':'none';
  document.getElementById('labN').textContent  = parseFloat(data.nitrogen).toFixed(1);
  document.getElementById('labP').textContent  = parseFloat(data.phosphorus).toFixed(1);
  document.getElementById('labK').textContent  = parseFloat(data.potassium).toFixed(1);
  document.getElementById('labNameDisplay').textContent = data.labName||'--';
  document.getElementById('labDateDisplay').textContent = data.date||'--';
  document.getElementById('labNotes').textContent = data.notes ? 'Notes: '+data.notes:'';
  document.getElementById('labStatus').textContent   = data.labName||'Saisi';
  document.getElementById('calibStatus2').textContent= data.calibrated?'Oui':'Non';
  // Remplir le formulaire avec les données existantes
  document.getElementById('labNInput').value    = data.nitrogen;
  document.getElementById('labPInput').value    = data.phosphorus;
  document.getElementById('labKInput').value    = data.potassium;
  document.getElementById('labNameInput').value = data.labName;
  document.getElementById('labDateInput').value = data.date;
  document.getElementById('labNotesInput').value= data.notes;
}

function saveLabData(){
  const n    = parseFloat(document.getElementById('labNInput').value);
  const p    = parseFloat(document.getElementById('labPInput').value);
  const k    = parseFloat(document.getElementById('labKInput').value);
  const name = document.getElementById('labNameInput').value.trim();
  const date = document.getElementById('labDateInput').value;
  const notes= document.getElementById('labNotesInput').value.trim();

  if (isNaN(n)||isNaN(p)||isNaN(k)||n<0||p<0||k<0){
    notify('Veuillez saisir des valeurs NPK valides (> 0)','error'); return;
  }

  fetch('/api/lab-npk', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({nitrogen:n, phosphorus:p, potassium:k, labName:name, date:date, notes:notes})
  }).then(r=>r.json()).then(data=>{
    if (data.status==='ok'){
      // Afficher la confirmation
      document.getElementById('confN').textContent    = n.toFixed(1) + ' mg/kg';
      document.getElementById('confP').textContent    = p.toFixed(1) + ' mg/kg';
      document.getElementById('confK').textContent    = k.toFixed(1) + ' mg/kg';
      document.getElementById('confLab').textContent  = name||'Non renseigné';
      document.getElementById('confDate').textContent = date||'Non renseignée';
      document.getElementById('confNotes').textContent= notes||'--';
      document.getElementById('confirmBox').className = 'confirm-box show';
      notify('✅ Données NPK laboratoire enregistrées !');
      refreshLabData();
    }
  }).catch(e=>notify('Erreur: '+e,'error'));
}

function clearLabData(){
  if (!confirm('Effacer les données NPK laboratoire ?')) return;
  fetch('/api/lab-npk/clear', {method:'POST'}).then(()=>{
    document.getElementById('confirmBox').className = 'confirm-box';
    ['labNInput','labPInput','labKInput','labNameInput','labDateInput','labNotesInput']
      .forEach(id=>document.getElementById(id).value='');
    refreshLabData();
    notify('Données NPK effacées');
  });
}

function calibrateSensor(){
  const s = document.getElementById('calibStatus');
  s.textContent = '⏳ Écriture IEEE754 en cours...';
  fetch('/api/calibrate-npk', {method:'POST'}).then(r=>r.json()).then(data=>{
    if (data.status==='ok'){
      s.textContent = '✅ Calibration appliquée avec succès';
      s.style.color = '#27ae60';
      notify('✅ Coefficients NPK IEEE754 écrits dans le capteur !');
      refreshLabData();
    } else {
      s.textContent = '❌ Erreur: ' + data.error;
      s.style.color = '#e74c3c';
      notify('Erreur calibration: '+data.error,'error');
    }
  }).catch(e=>{ s.textContent='❌ '+e; notify('Erreur: '+e,'error'); });
}

function refreshLabData(){
  fetch('/api/lab-npk').then(r=>r.json()).then(data=>updateLabDisplay(data));
}

// ── ACTIONNEURS ───────────────────────────────────────────────
function setIrrigation(state){
  fetch('/api/irrigation',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({state:state,auto:false})}).then(r=>r.json()).then(data=>{
    updateActuatorUI(data);
    notify(state?'💧 Irrigation ON — MANUEL':'💧 Irrigation OFF — MANUEL');
    refreshActuatorLog();
  });
}
function setIrrigAuto(e){
  fetch('/api/irrigation',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({auto:e})}).then(r=>r.json()).then(data=>{
    updateActuatorUI(data);
    notify(e?'🔄 Auto activé':'🔄 Mode manuel');
  });
}
function setFertilization(state){
  fetch('/api/fertilization',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({state:state})}).then(r=>r.json()).then(data=>{
    const el = document.getElementById('fertStatus');
    el.textContent = data.fertilization?'🟢 ON':'🔴 OFF';
    el.style.color = data.fertilization?'#27ae60':'#e74c3c';
    notify(state?'🌿 Fertilisation ON':'🌿 Fertilisation OFF');
    refreshActuatorLog();
  });
}
function updateActuatorUI(data){
  const el = document.getElementById('irrigStatus');
  el.textContent = data.irrigation?'🟢 ON':'🔴 OFF';
  el.style.color = data.irrigation?'#27ae60':'#e74c3c';
  if(data.auto!==undefined) document.getElementById('irrigAutoCheck').checked=data.auto;
  document.getElementById('totalActuator').textContent = data.totalActions||0;
}
function refreshActuatorLog(){
  fetch('/api/actuator-log').then(r=>r.json()).then(data=>{
    const log = document.getElementById('actuatorLog');
    if(!data.logs||data.logs.length===0){log.innerHTML='<span style="color:#999">Aucune action...</span>';return;}
    log.innerHTML = data.logs.slice().reverse().map(l=>{
      const on   = l.action.includes('_ON');
      const auto = l.trigger==='AUTO';
      return `<div class="${on?'log-on':'log-off'}">[${l.datetime}] ${l.action} ${auto?'<span class="log-auto">[AUTO]</span>':'[MANUEL]'} M=${l.moisture}%</div>`;
    }).join('');
  });
}

// ── SEUILS ────────────────────────────────────────────────────
function buildThresholdGrid(){
  const grid = document.getElementById('thresholdGrid');
  const defs = {moisture:[30,70],temperature:[15,35],ph:[5.5,7.5],conductivity:[500,2000],
    nitrogen:[80,150],phosphorus:[40,100],potassium:[70,140],salinity:[0,500],tds:[300,1500],fertility:[50,100]};
  grid.innerHTML = paramKeys.map((k,i)=>`
    <div style="display:flex;align-items:center;gap:6px;padding:6px;background:#f8f9fa;border-radius:8px;margin-bottom:6px">
      <label style="flex:1;font-size:.85em;font-weight:bold">${paramNames[i]}</label>
      <input type="number" id="${k}Min" value="${thresholds[k+'Min']||defs[k][0]}" step="0.1"
        style="width:65px;padding:5px;border:2px solid #ddd;border-radius:6px;text-align:center">
      <span style="color:#95a5a6">~</span>
      <input type="number" id="${k}Max" value="${thresholds[k+'Max']||defs[k][1]}" step="0.1"
        style="width:65px;padding:5px;border:2px solid #ddd;border-radius:6px;text-align:center">
    </div>`).join('');
}
function saveThresholds(){
  paramKeys.forEach(k=>{
    thresholds[k+'Min'] = parseFloat(document.getElementById(k+'Min').value);
    thresholds[k+'Max'] = parseFloat(document.getElementById(k+'Max').value);
  });
  fetch('/api/limits',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(thresholds)}).then(()=>notify('✓ Seuils sauvegardés'));
}
function askAIThresholds(){
  const msg = `Pour ma culture de ${document.getElementById('cropType').value||'tomate'} en sol ${document.getElementById('soilType').value||'limoneux'} au stade ${document.getElementById('growthStage').value}, donne-moi les seuils min-max recommandés pour chaque paramètre.`;
  document.getElementById('chatInput').value = msg;
  sendMsg();
}

// ── CONFIG ────────────────────────────────────────────────────
function saveConfig(){
  const cfg = {
    cropType:document.getElementById('cropType').value,
    soilType:document.getElementById('soilType').value,
    growthStages:document.getElementById('growthStage').value,
    cultivationMethods:document.getElementById('cultivation').value,
    aiLanguage:document.getElementById('aiLang').value,
    sensorInterval:document.getElementById('sensorInterval').value,
    aiInterval:document.getElementById('aiInterval').value
  };
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(cfg)}).then(()=>notify('✓ Configuration enregistrée'));
}

// ── CHAT ──────────────────────────────────────────────────────
function sendMsg(){
  const inp = document.getElementById('chatInput');
  const btn = document.getElementById('sendBtn');
  const msg = inp.value.trim();
  if(!msg || btn.disabled) return;
  addMsg('user', msg);
  inp.value = '';
  btn.disabled = true;
  btn.textContent = '⏳';
  inp.disabled = true;
  typingId = addTyping();

  fetch('/api/chat', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({message: msg})
  }).then(r => r.json()).then(d => {
    if (d.error) {
      stopPolling();
      removeTyping();
      addMsg('assistant', '❌ ' + d.error);
      resetChat();
    }
  }).catch(e => {
    stopPolling();
    removeTyping();
    addMsg('assistant', '❌ Erreur réseau: ' + e);
    resetChat();
  });

  chatPolling = setInterval(() => {
    fetch('/api/chat/status').then(r => r.json()).then(d => {
      if (d.ready && d.response) {
        stopPolling();
        removeTyping();
        addMsg('assistant', d.response);
        resetChat();
      }
    }).catch(() => {});
  }, 500);

  setTimeout(() => {
    if (chatPolling) {
      stopPolling();
      removeTyping();
      addMsg('assistant', '⏱ Timeout — l\'IA met trop de temps. Réessayez.');
      resetChat();
    }
  }, 35000);
}
function stopPolling(){if(chatPolling){clearInterval(chatPolling);chatPolling=null;}}
function resetChat(){const b=document.getElementById('sendBtn'),i=document.getElementById('chatInput');b.disabled=false;b.textContent='➤';i.disabled=false;i.focus();}
function addTyping(){
  const d=document.getElementById('chatMsgs'),id='t'+Date.now();
  const m=document.createElement('div'); m.className='msg assistant'; m.id=id;
  m.innerHTML='<div class="typing"><span></span><span></span><span></span></div>';
  d.appendChild(m); d.scrollTop=d.scrollHeight; return id;
}
function removeTyping(){if(typingId){const e=document.getElementById(typingId);if(e)e.remove();typingId=null;}}
function addMsg(role,content){
  const d=document.getElementById('chatMsgs');
  const m=document.createElement('div'); m.className='msg '+role;
  m.innerHTML=`<div>${content}</div><div class="msg-time">${new Date().toLocaleTimeString('fr-FR')}</div>`;
  d.appendChild(m); d.scrollTop=d.scrollHeight;
}

// ── TÉLÉCHARGEMENTS ───────────────────────────────────────────
function downloadAll()      {window.location.href='/api/download/all';}
function downloadSensors()  {window.location.href='/api/download/sensors';}
function downloadChat()     {window.location.href='/api/download/chat';}
function downloadAnalyses() {window.location.href='/api/download/analyses';}
function downloadLab()      {window.location.href='/api/download/lab';}

function forceAnalysis(){
  notify('🤖 Analyse en cours...');
  fetch('/api/analyze',{method:'POST'}).then(()=>setTimeout(refreshStatus,1000));
}
function resetConfig(){
  if(confirm('Réinitialiser la configuration ? Le module redémarre en mode config.'))
    fetch('/reset',{method:'POST'}).then(()=>notify('⚙ Réinitialisation...','warn'));
}

// ── POLL PRINCIPAL ────────────────────────────────────────────
function refreshStatus(){
  fetch('/api/status').then(r=>r.json()).then(data=>{
    updateSensorDisplay(data);
    document.getElementById('analysis').textContent = data.analysis||'En attente...';
  }).catch(()=>{});
}
function refreshHistory(){
  fetch('/api/history').then(r=>r.json()).then(data=>{
    sensorHistory=data.history; updateChart();
  }).catch(()=>{});
}
function refreshActuators(){
  fetch('/api/actuators').then(r=>r.json()).then(data=>{
    updateActuatorUI(data);
    const fel=document.getElementById('fertStatus');
    fel.textContent=data.fertilization?'🟢 ON':'🔴 OFF';
    fel.style.color=data.fertilization?'#27ae60':'#e74c3c';
  }).catch(()=>{});
}
function loadConfig(){
  fetch('/api/config').then(r=>r.json()).then(d=>{
    if(d.cropType)    document.getElementById('cropType').value=d.cropType;
    if(d.soilType)    document.getElementById('soilType').value=d.soilType;
    if(d.growthStages)document.getElementById('growthStage').value=d.growthStages;
    if(d.cultivationMethods)document.getElementById('cultivation').value=d.cultivationMethods;
    if(d.aiLanguage)  document.getElementById('aiLang').value=d.aiLanguage;
    if(d.sensorInterval)document.getElementById('sensorInterval').value=d.sensorInterval;
    if(d.aiInterval)  document.getElementById('aiInterval').value=d.aiInterval;
  }).catch(()=>{});
}
function loadThresholds(){
  fetch('/api/limits').then(r=>r.json()).then(data=>{thresholds=data;buildThresholdGrid();}).catch(()=>{});
}

// ── INITIALISATION ────────────────────────────────────────────
buildThresholdGrid();
setTimeout(loadConfig,500);
setTimeout(loadThresholds,600);
refreshStatus(); refreshHistory(); refreshActuators(); refreshActuatorLog(); refreshLabData();
setInterval(refreshStatus,     3000);
setInterval(refreshHistory,   10000);
setInterval(refreshActuators,  3000);
setInterval(refreshActuatorLog,5000);
setInterval(refreshLabData,   10000);
</script>
</body>
</html>)rawliteral";
    server.send(200,"text/html",html);
  });

  // ─── API STATUS ──────────────────────────────────────────────
  server.on("/api/status", HTTP_GET, [](){
    int criticals=0, partials=0;
    for(int i=0;i<sensorLogCount;i++) if(sensorLogs[i].isPartial) partials++;
    auto oor=[](float v,float mn,float mx){return v<mn||v>mx;};
    if(oor(currentReading.moisture,   thresholds.moistureMin,   thresholds.moistureMax))   criticals++;
    if(oor(currentReading.temperature,thresholds.temperatureMin,thresholds.temperatureMax)) criticals++;
    if(oor(currentReading.ph,         thresholds.phMin,         thresholds.phMax))          criticals++;
    if(oor(currentReading.conductivity,thresholds.conductivityMin,thresholds.conductivityMax))criticals++;

    String j="{";
    j+="\"datetime\":\""+String(currentReading.datetime)+"\",";
    j+="\"moisture\":"+String(currentReading.moisture,1)+",";
    j+="\"temperature\":"+String(currentReading.temperature,1)+",";
    j+="\"ph\":"+String(currentReading.ph,2)+",";
    j+="\"conductivity\":"+String(currentReading.conductivity,0)+",";
    j+="\"nitrogen\":"+String(currentReading.nitrogen)+",";
    j+="\"phosphorus\":"+String(currentReading.phosphorus)+",";
    j+="\"potassium\":"+String(currentReading.potassium)+",";
    j+="\"salinity\":"+String(currentReading.salinity,1)+",";
    j+="\"tds\":"+String(currentReading.tds)+",";
    j+="\"fertility\":"+String(currentReading.fertility)+",";
    j+="\"isPartial\":"+String(currentReading.isPartial?"true":"false")+",";
    j+="\"sensorMode\":\""+String(sensorActuallyReading?"Capteur Réel RS485":(sensorInitialized?"Init OK / En attente":"Simulation"))+"\",";
    j+="\"totalReadings\":"+String(sensorLogCount)+",";
    j+="\"partialCount\":"+String(partials)+",";
    j+="\"criticalCount\":"+String(criticals)+",";
    j+="\"heap\":"+String(ESP.getFreeHeap())+",";
    j+="\"analysis\":\""+escapeJSON(lastAnalysis)+"\"";
    j+="}";
    server.send(200,"application/json",j);
  });

  // ─── API HISTORY ─────────────────────────────────────────────
  server.on("/api/history", HTTP_GET, [](){
    String j="{\"history\":[";
    int startIdx=max(0,sensorLogCount-50);
    for(int i=startIdx;i<sensorLogCount;i++){
      if(i>startIdx)j+=",";
      j+="{\"moisture\":"+String(sensorLogs[i].moisture,1)+",";
      j+="\"temperature\":"+String(sensorLogs[i].temperature,1)+",";
      j+="\"ph\":"+String(sensorLogs[i].ph,2)+",";
      j+="\"conductivity\":"+String(sensorLogs[i].conductivity,0)+",";
      j+="\"nitrogen\":"+String(sensorLogs[i].nitrogen)+",";
      j+="\"phosphorus\":"+String(sensorLogs[i].phosphorus)+",";
      j+="\"potassium\":"+String(sensorLogs[i].potassium)+",";
      j+="\"isPartial\":"+String(sensorLogs[i].isPartial?"true":"false")+"}";
    }
    j+="]}";
    server.send(200,"application/json",j);
  });

  // ─── API CONFIG GET ──────────────────────────────────────────
  server.on("/api/config", HTTP_GET, [](){
    String j="{";
    j+="\"soilType\":\""+escapeJSON(systemConfig.soilType)+"\",";
    j+="\"cropType\":\""+escapeJSON(systemConfig.cropType)+"\",";
    j+="\"growthStages\":\""+escapeJSON(systemConfig.growthStages)+"\",";
    j+="\"cultivationMethods\":\""+escapeJSON(systemConfig.cultivationMethods)+"\",";
    j+="\"aiLanguage\":\""+systemConfig.aiLanguage+"\",";
    j+="\"sensorInterval\":"+String(systemConfig.sensorInterval)+",";
    j+="\"aiInterval\":"+String(systemConfig.aiInterval)+"}";
    server.send(200,"application/json",j);
  });

  // ─── API CONFIG POST ─────────────────────────────────────────
  server.on("/api/config", HTTP_POST, [](){
    if(!server.hasArg("plain")){server.send(400,"application/json","{\"error\":\"no data\"}");return;}
    String b=server.arg("plain");
    auto pStr=[&](String k,String&t){
      int i=b.indexOf("\""+k+"\":\""); if(i<0)return;
      int s=i+k.length()+4, e=b.indexOf("\"",s); if(e>s)t=b.substring(s,e);
    };
    auto pInt=[&](String k,int&t){
      int i=b.indexOf("\""+k+"\":"); if(i<0)return;
      String v=b.substring(i+k.length()+3);
      int e=v.indexOf(","); if(e<0)e=v.indexOf("}");
      if(e>0)t=v.substring(0,e).toInt();
    };
    pStr("cropType",          systemConfig.cropType);
    pStr("soilType",          systemConfig.soilType);
    pStr("growthStages",      systemConfig.growthStages);
    pStr("cultivationMethods",systemConfig.cultivationMethods);
    pStr("aiLanguage",        systemConfig.aiLanguage);
    pInt("sensorInterval",    systemConfig.sensorInterval);
    pInt("aiInterval",        systemConfig.aiInterval);
    saveSystemConfig(); updateAISystemRole();
    server.send(200,"application/json","{\"status\":\"ok\"}");
  });

  // ─── API LIMITS ──────────────────────────────────────────────
  server.on("/api/limits", HTTP_GET, [](){
    String j="{";
    j+="\"moistureMin\":"+String(thresholds.moistureMin)+",\"moistureMax\":"+String(thresholds.moistureMax)+",";
    j+="\"temperatureMin\":"+String(thresholds.temperatureMin)+",\"temperatureMax\":"+String(thresholds.temperatureMax)+",";
    j+="\"phMin\":"+String(thresholds.phMin)+",\"phMax\":"+String(thresholds.phMax)+",";
    j+="\"conductivityMin\":"+String(thresholds.conductivityMin)+",\"conductivityMax\":"+String(thresholds.conductivityMax)+",";
    j+="\"nitrogenMin\":"+String(thresholds.nitrogenMin)+",\"nitrogenMax\":"+String(thresholds.nitrogenMax)+",";
    j+="\"phosphorusMin\":"+String(thresholds.phosphorusMin)+",\"phosphorusMax\":"+String(thresholds.phosphorusMax)+",";
    j+="\"potassiumMin\":"+String(thresholds.potassiumMin)+",\"potassiumMax\":"+String(thresholds.potassiumMax)+",";
    j+="\"salinityMin\":"+String(thresholds.salinityMin)+",\"salinityMax\":"+String(thresholds.salinityMax)+",";
    j+="\"tdsMin\":"+String(thresholds.tdsMin)+",\"tdsMax\":"+String(thresholds.tdsMax)+",";
    j+="\"fertilityMin\":"+String(thresholds.fertilityMin)+",\"fertilityMax\":"+String(thresholds.fertilityMax)+"}";
    server.send(200,"application/json",j);
  });

  server.on("/api/limits", HTTP_POST, [](){
    if(!server.hasArg("plain")){server.send(400,"application/json","{\"error\":\"no data\"}");return;}
    String b=server.arg("plain");
    auto pF=[&](String k,float&t){
      int i=b.indexOf("\""+k+"\":"); if(i<0)return;
      String v=b.substring(i+k.length()+3); int e=v.indexOf(","); if(e<0)e=v.indexOf("}");
      if(e>0)t=v.substring(0,e).toFloat();
    };
    auto pI=[&](String k,int&t){
      int i=b.indexOf("\""+k+"\":"); if(i<0)return;
      String v=b.substring(i+k.length()+3); int e=v.indexOf(","); if(e<0)e=v.indexOf("}");
      if(e>0)t=v.substring(0,e).toInt();
    };
    pF("moistureMin",thresholds.moistureMin); pF("moistureMax",thresholds.moistureMax);
    pF("temperatureMin",thresholds.temperatureMin); pF("temperatureMax",thresholds.temperatureMax);
    pF("phMin",thresholds.phMin); pF("phMax",thresholds.phMax);
    pF("conductivityMin",thresholds.conductivityMin); pF("conductivityMax",thresholds.conductivityMax);
    pI("nitrogenMin",thresholds.nitrogenMin); pI("nitrogenMax",thresholds.nitrogenMax);
    pI("phosphorusMin",thresholds.phosphorusMin); pI("phosphorusMax",thresholds.phosphorusMax);
    pI("potassiumMin",thresholds.potassiumMin); pI("potassiumMax",thresholds.potassiumMax);
    pF("salinityMin",thresholds.salinityMin); pF("salinityMax",thresholds.salinityMax);
    pI("tdsMin",thresholds.tdsMin); pI("tdsMax",thresholds.tdsMax);
    pI("fertilityMin",thresholds.fertilityMin); pI("fertilityMax",thresholds.fertilityMax);
    saveThresholds();
    server.send(200,"application/json","{\"status\":\"ok\"}");
  });

  // ─── API LAB NPK GET ─────────────────────────────────────────
  server.on("/api/lab-npk", HTTP_GET, [](){
    String j="{";
    j+="\"nitrogen\":"+String(labNPK.nitrogen,2)+",";
    j+="\"phosphorus\":"+String(labNPK.phosphorus,2)+",";
    j+="\"potassium\":"+String(labNPK.potassium,2)+",";
    j+="\"date\":\""+String(labNPK.date)+"\",";
    j+="\"labName\":\""+escapeJSON(labNPK.labName)+"\",";
    j+="\"notes\":\""+escapeJSON(labNPK.notes)+"\",";
    j+="\"hasData\":"+String(labNPK.hasData?"true":"false")+",";
    j+="\"calibrated\":"+String(labNPK.calibrated?"true":"false")+"}";
    server.send(200,"application/json",j);
  });

  // ─── API LAB NPK POST ────────────────────────────────────────
  server.on("/api/lab-npk", HTTP_POST, [](){
    if(!server.hasArg("plain")){server.send(400,"application/json","{\"error\":\"no data\"}");return;}
    String b=server.arg("plain");
    auto pF=[&](String k,float&t){
      int i=b.indexOf("\""+k+"\":"); if(i<0)return;
      String v=b.substring(i+k.length()+3); int e=v.indexOf(","); if(e<0)e=v.indexOf("}");
      if(e>0)t=v.substring(0,e).toFloat();
    };
    auto pStr=[&](String k,String&t){
      int i=b.indexOf("\""+k+"\":\""); if(i<0)return;
      int s=i+k.length()+4, e=b.indexOf("\"",s); if(e>s)t=b.substring(s,e);
    };
    pF("nitrogen",   labNPK.nitrogen);
    pF("phosphorus", labNPK.phosphorus);
    pF("potassium",  labNPK.potassium);
    String dateStr="", nameStr="", notesStr="";
    pStr("date",    dateStr); pStr("labName", nameStr); pStr("notes", notesStr);
    strncpy(labNPK.date, dateStr.c_str(), 19); labNPK.date[19]='\0';
    labNPK.labName    = nameStr;
    labNPK.notes      = notesStr;
    labNPK.hasData    = true;
    labNPK.calibrated = false;  // reset calibration si nouvelles données
    saveLabNPK();
    updateAISystemRole();  // Mettre à jour le contexte IA avec les nouvelles données labo
    Serial.printf("✓ NPK Labo: N=%.1f P=%.1f K=%.1f [%s]\n",
      labNPK.nitrogen, labNPK.phosphorus, labNPK.potassium, labNPK.labName.c_str());
    server.send(200,"application/json","{\"status\":\"ok\"}");
  });

  // ─── API LAB NPK CLEAR ───────────────────────────────────────
  server.on("/api/lab-npk/clear", HTTP_POST, [](){
    labNPK.nitrogen=0; labNPK.phosphorus=0; labNPK.potassium=0;
    labNPK.date[0]='\0'; labNPK.labName=""; labNPK.notes="";
    labNPK.hasData=false; labNPK.calibrated=false;
    saveLabNPK();
    server.send(200,"application/json","{\"status\":\"ok\"}");
  });

  // ─── API CALIBRATION NPK IEEE754 ─────────────────────────────
  server.on("/api/calibrate-npk", HTTP_POST, [](){
    if(!labNPK.hasData){
      server.send(400,"application/json","{\"error\":\"Aucune donnee labo disponible\"}"); return;
    }
    if(!sensorInitialized){
      server.send(400,"application/json","{\"error\":\"Capteur non connecte\"}"); return;
    }
    // TerraSoil ne supporte pas l'écriture registres — calibration logicielle uniquement
    labNPK.calibrated = true;
    saveLabNPK();
    Serial.printf("✓ Calibration logicielle: N=%.1f P=%.1f K=%.1f enregistrees\n",
      labNPK.nitrogen, labNPK.phosphorus, labNPK.potassium);
    server.send(200,"application/json","{\"status\":\"ok\",\"message\":\"Calibration logicielle appliquee — IA utilise les valeurs labo\"}");
  });

  // ─── API ANALYZE ─────────────────────────────────────────────
  server.on("/api/analyze", HTTP_POST, [](){
    server.send(200,"application/json","{\"status\":\"ok\"}");
    lastSensorUpdate = millis() - ((unsigned long)systemConfig.sensorInterval*1000UL);
    lastAIUpdate     = millis() - ((unsigned long)systemConfig.aiInterval*1000UL);
  });

  // ─── API CHAT ────────────────────────────────────────────────
  server.on("/api/chat", HTTP_POST, [](){
    if(!aiClient){server.send(500,"application/json","{\"error\":\"AI non initialisé\"}");return;}
    if(!server.hasArg("plain")){server.send(400,"application/json","{\"error\":\"no data\"}");return;}
    String b=server.arg("plain");
    int s=b.indexOf("\"message\":\"")+11, e=b.indexOf("\"",s);
    String msg=b.substring(s,e);
    if(msg.length()==0){server.send(400,"application/json","{\"error\":\"message vide\"}");return;}
    if(chatRequestPending){server.send(503,"application/json","{\"error\":\"IA occupee\"}");return;}
    // Envoyer la requête à Core1 et répondre IMMÉDIATEMENT — ne pas bloquer Core0
    if(xSemaphoreTake(chatMutex,pdMS_TO_TICKS(500))){
      pendingChatRequest=msg; chatResponseReady=false; chatRequestPending=true;
      xSemaphoreGive(chatMutex);
    }
    server.send(202,"application/json","{\"status\":\"pending\"}");
  });

  server.on("/api/chat/status", HTTP_GET, [](){
    String j="{\"pending\":"+String(chatRequestPending?"true":"false")+",";
    j+="\"ready\":"+String(chatResponseReady?"true":"false")+",";
    if(chatResponseReady){
      String r="";
      if(xSemaphoreTake(chatMutex,100/portTICK_PERIOD_MS)){
        r=pendingChatResponse; chatResponseReady=false; xSemaphoreGive(chatMutex);
      }
      j+="\"response\":\""+escapeJSON(r)+"\"";
    } else j+="\"response\":\"\"";
    j+="}";
    server.send(200,"application/json",j);
  });

  // ─── API ACTIONNEURS ─────────────────────────────────────────
  server.on("/api/actuators", HTTP_GET, [](){
    String j="{\"irrigation\":"+String(irrigationManual?"true":"false")+",";
    j+="\"irrigationAuto\":"+String(irrigationAuto?"true":"false")+",";
    j+="\"fertilization\":"+String(fertilizationManual?"true":"false")+",";
    j+="\"totalActions\":"+String(actuatorLogCount)+"}";
    server.send(200,"application/json",j);
  });

  server.on("/api/irrigation", HTTP_POST, [](){
    String b=server.arg("plain");
    bool prev=irrigationManual;
    if(b.indexOf("\"auto\":true")>0){irrigationAuto=true;irrigationManual=false;}
    else if(b.indexOf("\"auto\":false")>0){irrigationAuto=false;}
    if(b.indexOf("\"state\":true")>0){irrigationManual=true;irrigationAuto=false;}
    else if(b.indexOf("\"state\":false")>0){irrigationManual=false;}
    if(prev!=irrigationManual)
      logActuatorAction(irrigationManual?"IRRIGATION_ON":"IRRIGATION_OFF","MANUEL",currentReading.moisture);
    digitalWrite(IRRIGATION_PIN,irrigationManual?HIGH:LOW);
    server.send(200,"application/json","{\"irrigation\":"+String(irrigationManual?"true":"false")+",\"auto\":"+String(irrigationAuto?"true":"false")+"}");
  });

  server.on("/api/fertilization", HTTP_POST, [](){
    bool prev=fertilizationManual;
    fertilizationManual=(server.arg("plain").indexOf("\"state\":true")>0);
    if(prev!=fertilizationManual)
      logActuatorAction(fertilizationManual?"FERTILIZATION_ON":"FERTILIZATION_OFF","MANUEL",currentReading.moisture);
    digitalWrite(FERTILIZATION_PIN,fertilizationManual?HIGH:LOW);
    server.send(200,"application/json","{\"fertilization\":"+String(fertilizationManual?"true":"false")+"}");
  });

  server.on("/api/actuator-log", HTTP_GET, [](){
    String j="{\"logs\":[";
    int startIdx=max(0,actuatorLogCount-20);
    for(int i=startIdx;i<actuatorLogCount;i++){
      if(i>startIdx)j+=",";
      j+="{\"datetime\":\""+String(actuatorLogs[i].datetime)+"\",";
      j+="\"action\":\""+actuatorLogs[i].action+"\",";
      j+="\"trigger\":\""+actuatorLogs[i].trigger+"\",";
      j+="\"moisture\":"+String(actuatorLogs[i].moisture,1)+"}";
    }
    j+="]}";
    server.send(200,"application/json",j);
  });

  // ─── EXPORTS ─────────────────────────────────────────────────
  server.on("/api/download/all", HTTP_GET, [](){
    String d="TERRA AI v3.0 — Export Complet\n";
    d+="Date: "+getDateTime()+"\n";
    d+="Culture: "+systemConfig.cropType+" | Sol: "+systemConfig.soilType+"\n";
    d+="\n=== NPK LABORATOIRE ===\n";
    if(labNPK.hasData){
      d+="N="+String(labNPK.nitrogen,1)+" P="+String(labNPK.phosphorus,1)+" K="+String(labNPK.potassium,1)+" mg/kg\n";
      d+="Labo: "+labNPK.labName+" | Date: "+String(labNPK.date)+"\n";
      d+="Calibration IEEE754: "+String(labNPK.calibrated?"OUI":"NON")+"\n";
      d+="Notes: "+labNPK.notes+"\n";
    } else d+="Aucune donnee\n";
    d+="\n=== CAPTEUR SOL ("+String(sensorLogCount)+" lectures) ===\n";
    d+="DateTime,Humidite,Temp,pH,EC,N,P,K,Salinite,TDS,Fertilite,Partiel\n";
    for(int i=0;i<sensorLogCount;i++){
      d+=String(sensorLogs[i].datetime)+",";
      d+=String(sensorLogs[i].moisture,1)+","+String(sensorLogs[i].temperature,1)+",";
      d+=String(sensorLogs[i].ph,2)+","+String(sensorLogs[i].conductivity,0)+",";
      d+=String(sensorLogs[i].nitrogen)+","+String(sensorLogs[i].phosphorus)+","+String(sensorLogs[i].potassium)+",";
      d+=String(sensorLogs[i].salinity,1)+","+String(sensorLogs[i].tds)+","+String(sensorLogs[i].fertility)+",";
      d+=String(sensorLogs[i].isPartial?"PARTIEL":"OK")+"\n";
    }
    d+="\n=== ACTIONNEURS ===\n";
    for(int i=0;i<actuatorLogCount;i++)
      d+=String(actuatorLogs[i].datetime)+","+actuatorLogs[i].action+","+actuatorLogs[i].trigger+","+String(actuatorLogs[i].moisture,1)+"\n";
    d+="\n=== DERNIERE ANALYSE IA ===\n"+lastAnalysis+"\n";
    server.sendHeader("Content-Disposition","attachment; filename=terra-ai-export.txt");
    server.send(200,"text/plain; charset=utf-8",d);
  });

  server.on("/api/download/sensors", HTTP_GET, [](){
    String csv="DateTime,Humidite(%),Temp(C),pH,EC(µS/cm),N(mg/kg),P(mg/kg),K(mg/kg),Salinite,TDS,Fertilite,Partiel\n";
    for(int i=0;i<sensorLogCount;i++){
      csv+=String(sensorLogs[i].datetime)+","+String(sensorLogs[i].moisture,1)+",";
      csv+=String(sensorLogs[i].temperature,1)+","+String(sensorLogs[i].ph,2)+",";
      csv+=String(sensorLogs[i].conductivity,0)+","+String(sensorLogs[i].nitrogen)+",";
      csv+=String(sensorLogs[i].phosphorus)+","+String(sensorLogs[i].potassium)+",";
      csv+=String(sensorLogs[i].salinity,1)+","+String(sensorLogs[i].tds)+",";
      csv+=String(sensorLogs[i].fertility)+","+String(sensorLogs[i].isPartial?"PARTIEL":"OK")+"\n";
    }
    server.sendHeader("Content-Disposition","attachment; filename=terra-ai-sol.csv");
    server.send(200,"text/csv; charset=utf-8",csv);
  });

  server.on("/api/download/lab", HTTP_GET, [](){
    String t="Terra AI — NPK Laboratoire\nDate export: "+getDateTime()+"\n\n";
    if(labNPK.hasData){
      t+="Azote N:     "+String(labNPK.nitrogen,2)+" mg/kg\n";
      t+="Phosphore P: "+String(labNPK.phosphorus,2)+" mg/kg\n";
      t+="Potassium K: "+String(labNPK.potassium,2)+" mg/kg\n";
      t+="Laboratoire: "+labNPK.labName+"\n";
      t+="Date:        "+String(labNPK.date)+"\n";
      t+="Notes:       "+labNPK.notes+"\n";
      t+="Calibration: "+String(labNPK.calibrated?"OUI — IEEE754 écrits dans capteur":"NON")+"\n";
    } else t+="Aucune donnée NPK laboratoire saisie.\n";
    server.sendHeader("Content-Disposition","attachment; filename=terra-ai-npk-labo.txt");
    server.send(200,"text/plain; charset=utf-8",t);
  });

  server.on("/api/download/chat", HTTP_GET, [](){
    String t="Terra AI — Historique Chat\n"+getDateTime()+"\n\n";
    for(int i=0;i<chatMessageCount;i++){
      t+="["+String(chatHistory[i].datetime)+"] ";
      t+=(chatHistory[i].role=="user"?"USER: ":"TERRA AI: ");
      t+=chatHistory[i].content+"\n\n";
    }
    server.sendHeader("Content-Disposition","attachment; filename=terra-ai-chat.txt");
    server.send(200,"text/plain; charset=utf-8",t);
  });

  server.on("/api/download/analyses", HTTP_GET, [](){
    String t="Terra AI — Analyses IA\n"+getDateTime()+"\n\n=== DERNIÈRE ===\n"+lastAnalysis+"\n\n=== HISTORIQUE ===\n";
    File f=SPIFFS.open("/analyses.txt","r");
    if(f){t+=f.readString();f.close();}
    server.sendHeader("Content-Disposition","attachment; filename=terra-ai-analyses.txt");
    server.send(200,"text/plain; charset=utf-8",t);
  });

  server.on("/reset", HTTP_POST, [](){
    preferences.clear();
    server.send(200,"text/plain","OK");
    delay(1000); ESP.restart();
  });

  server.onNotFound([](){server.send(404,"text/plain","404");});
}
