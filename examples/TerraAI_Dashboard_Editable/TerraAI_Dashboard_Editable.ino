#include <WiFi.h>
#include <TerraSoil.h>
#include <Preferences.h>

// --- Configuration WiFi ---
const char* ssid = "wifi";
const char* password = "passeword";

// --- Configuration TerraSoil & Pins xiao esp32 s3 - if you have esp32 change  ---
#define RS485_RX_PIN   D7  // D7
#define RS485_TX_PIN   D6  // D6
#define RS485_RTS_PIN  D1   // D1

HardwareSerial RS485Serial(1);
TerraSoil sensor(&RS485Serial, RS485_RTS_PIN);
TerraSoilData data;

WiFiServer server(80);
Preferences preferences;

// Variables pour stocker les informations (chargées depuis EEPROM)
String deviceId = "P001";
String plotNumber = "Parcel 1";
String soilType = "Loamy Soil";
String cropType = "Tomato";

void setup() {
  Serial.begin(115200);
  
  // Initialiser Preferences (stockage permanent)
  preferences.begin("terraai", false);
  
  // Charger les données sauvegardées
  deviceId = preferences.getString("deviceId", "P001");
  plotNumber = preferences.getString("plotNumber", "Parcel #12");
  soilType = preferences.getString("soilType", "Loamy Soil");
  cropType = preferences.getString("cropType", "Tomato");
  
  Serial.println("=== TERRA AI - Smart Agriculture ===");
  Serial.println("Loaded Config:");
  Serial.println("  ID: " + deviceId);
  Serial.println("  Plot: " + plotNumber);
  Serial.println("  Soil: " + soilType);
  Serial.println("  Crop: " + cropType);
  
  // Initialisation Capteur
  if (sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800)) {
    Serial.println("✓ TerraSoil Sensor Ready");
  }

  // Connexion WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✓ WiFi Connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("✓ Web Server Started");
  Serial.println("=====================================\n");
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String request = "";
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            // ========== TRAITER LES REQUÊTES POST (Sauvegarde) ==========
            if (request.indexOf("POST /save") >= 0) {
              // Extraire les données du formulaire
              int bodyStart = request.indexOf("\r\n\r\n");
              if (bodyStart != -1) {
                String body = request.substring(bodyStart + 4);
                
                // Parser les données
                deviceId = extractParam(body, "deviceId");
                plotNumber = extractParam(body, "plotNumber");
                soilType = extractParam(body, "soilType");
                cropType = extractParam(body, "cropType");
                
                // Sauvegarder dans la mémoire permanente
                preferences.putString("deviceId", deviceId);
                preferences.putString("plotNumber", plotNumber);
                preferences.putString("soilType", soilType);
                preferences.putString("cropType", cropType);
                
                Serial.println("✓ Configuration saved:");
                Serial.println("  ID: " + deviceId);
                Serial.println("  Plot: " + plotNumber);
                Serial.println("  Soil: " + soilType);
                Serial.println("  Crop: " + cropType);
                
                // Rediriger vers la page principale
                client.println("HTTP/1.1 303 See Other");
                client.println("Location: /");
                client.println("Connection: close");
                client.println();
              }
            }
            
            // ========== PAGE CONFIGURATION ==========
            else if (request.indexOf("GET /config") >= 0) {
              sendConfigPage(client);
            }
            
            // ========== PAGE PRINCIPALE (Dashboard) ==========
            else {
              // Lecture des données capteur
              sensor.readSensor(data);
              sendDashboardPage(client);
            }
            
            break;
          } else { 
            currentLine = ""; 
          }
        } else if (c != '\r') { 
          currentLine += c; 
        }
      }
    }
    client.stop();
  }
}

// ========== FONCTION: Extraire paramètre depuis POST ==========
String extractParam(String data, String param) {
  String searchStr = param + "=";
  int start = data.indexOf(searchStr);
  if (start == -1) return "";
  
  start += searchStr.length();
  int end = data.indexOf("&", start);
  if (end == -1) end = data.length();
  
  String value = data.substring(start, end);
  value.replace("+", " ");
  value = urlDecode(value);
  return value;
}

// ========== FONCTION: Décoder URL ==========
String urlDecode(String str) {
  String decoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == '%') {
      i++;
      code0 = str.charAt(i);
      i++;
      code1 = str.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      decoded += c;
    } else {
      decoded += c;
    }
  }
  return decoded;
}

char h2int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

// ========== PAGE DASHBOARD ==========
void sendDashboardPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  
  client.print("<!DOCTYPE html><html lang='en'><head>");
  client.print("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.print("<meta http-equiv='refresh' content='5'>");
  client.print("<link href='https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600&display=swap' rel='stylesheet'>");
  client.print("<style>");
  client.print("body{font-family:'Poppins',sans-serif; background:linear-gradient(135deg, #1e3c31 0%, #2d5a27 100%); color:#fff; margin:0; padding:20px; min-height:100vh;}");
  client.print(".container{max-width:900px; margin:auto;}");
  client.print(".header{text-align:center; padding:20px; background:rgba(255,255,255,0.1); border-radius:15px; backdrop-filter:blur(10px); margin-bottom:20px; border:1px solid rgba(255,255,255,0.2);}");
  client.print(".info-bar{display:grid; grid-template-columns:repeat(auto-fit, minmax(150px, 1fr)); gap:10px; margin-bottom:20px;}");
  client.print(".info-item{background:#8d6e63; padding:10px; border-radius:10px; font-size:0.8em; text-align:center;}");
  client.print(".grid{display:grid; grid-template-columns:repeat(auto-fit, minmax(200px, 1fr)); gap:15px;}");
  client.print(".card{background:rgba(255,255,255,0.9); color:#333; padding:20px; border-radius:15px; text-align:center; border-left:5px solid #4CAF50; transition:0.3s;}");
  client.print(".card:hover{transform:translateY(-5px); box-shadow:0 10px 20px rgba(0,0,0,0.2);}");
  client.print(".val{font-size:2em; font-weight:600; color:#2d5a27;} .unit{font-size:0.5em; color:#666;} .label{font-size:0.9em; color:#888; text-transform:uppercase;}");
  client.print("h1{margin:0; color:#fff; letter-spacing:2px;} .status{color:#a5d6a7; font-size:0.8em;}");
  client.print(".config-btn{display:inline-block; margin-top:10px; padding:10px 20px; background:#4CAF50; color:white; text-decoration:none; border-radius:5px; font-size:0.9em;}");
  client.print(".config-btn:hover{background:#45a049;}");
  client.print("</style></head><body>");

  client.print("<div class='container'>");
  client.print("<div class='header'>");
  client.print("<h1> TERRA AI</h1>");
  client.print("<p class='status'>SMART AGRICULTURE SYSTEM  ONLINE</p>");
  client.print("<a href='/config' class='config-btn' Edit Parcel Info</a>");
  client.print("</div>");

  // Barre d'info Metadata
  client.print("<div class='info-bar'>");
  client.print("<div class='info-item'><b>ID:</b> " + deviceId + "</div>");
  client.print("<div class='info-item'><b>PARCEL:</b> " + plotNumber + "</div>");
  client.print("<div class='info-item'><b>SOIL:</b> " + soilType + "</div>");
  client.print("<div class='info-item'><b>PLANT:</b> " + cropType + "</div>");
  client.print("</div>");

  // Grille des 10 données
  client.print("<div class='grid'>");
  renderCard(client, "Moisture", data.moisture, "%");
  renderCard(client, "Temperature", data.temperature, "C");
  renderCard(client, "Conductivity", data.conductivity, "S/cm");
  renderCard(client, "Soil pH", data.ph, "pH");
  renderCard(client, "Nitrogen (N)", data.nitrogen, "mg/kg");
  renderCard(client, "Phosphorus (P)", data.phosphorus, "mg/kg");
  renderCard(client, "Potassium (K)", data.potassium, "mg/kg");
  renderCard(client, "Salinity", data.salinity, "");
  renderCard(client, "TDS", data.tds, "mg/L");
  renderCard(client, "Fertility", data.fertility, "mg/kg");
  client.print("</div></div></body></html>");
}

// ========== PAGE CONFIGURATION ==========
void sendConfigPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  
  client.print("<!DOCTYPE html><html lang='en'><head>");
  client.print("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.print("<link href='https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600&display=swap' rel='stylesheet'>");
  client.print("<style>");
  client.print("body{font-family:'Poppins',sans-serif; background:linear-gradient(135deg, #1e3c31 0%, #2d5a27 100%); color:#fff; margin:0; padding:20px; min-height:100vh;}");
  client.print(".container{max-width:600px; margin:auto; background:rgba(255,255,255,0.95); padding:30px; border-radius:15px; color:#333;}");
  client.print("h1{color:#2d5a27; text-align:center; margin-bottom:30px;}");
  client.print(".form-group{margin-bottom:20px;}");
  client.print("label{display:block; font-weight:600; margin-bottom:5px; color:#555;}");
  client.print("input{width:100%; padding:12px; border:2px solid #ddd; border-radius:8px; font-size:1em; box-sizing:border-box;}");
  client.print("input:focus{outline:none; border-color:#4CAF50;}");
  client.print(".btn-group{display:flex; gap:10px; margin-top:30px;}");
  client.print("button, .back-btn{flex:1; padding:12px; border:none; border-radius:8px; font-size:1em; font-weight:600; cursor:pointer; text-align:center; text-decoration:none; display:block;}");
  client.print("button{background:#4CAF50; color:white;} button:hover{background:#45a049;}");
  client.print(".back-btn{background:#757575; color:white;} .back-btn:hover{background:#616161;}");
  client.print("</style></head><body>");
  
  client.print("<div class='container'>");
  client.print("<h1>Edit Parcel Information</h1>");
  client.print("<form method='POST' action='/save'>");
  
  client.print("<div class='form-group'>");
  client.print("<label for='deviceId'>Parcel ID</label>");
  client.print("<input type='text' id='deviceId' name='deviceId' value='" + deviceId + "' required>");
  client.print("</div>");
  
  client.print("<div class='form-group'>");
  client.print("<label for='plotNumber'>Parcel Number</label>");
  client.print("<input type='text' id='plotNumber' name='plotNumber' value='" + plotNumber + "' required>");
  client.print("</div>");
  
  client.print("<div class='form-group'>");
  client.print("<label for='soilType'>Soil Type</label>");
  client.print("<input type='text' id='soilType' name='soilType' value='" + soilType + "' required>");
  client.print("</div>");
  
  client.print("<div class='form-group'>");
  client.print("<label for='cropType'>Plant Type</label>");
  client.print("<input type='text' id='cropType' name='cropType' value='" + cropType + "' required>");
  client.print("</div>");
  
  client.print("<div class='btn-group'>");
  client.print("<a href='/' class='back-btn'>Cancel</a>");
  client.print("<button type='submit'>Save Changes</button>");
  client.print("</div>");
  
  client.print("</form>");
  client.print("</div></body></html>");
}

// ========== FONCTION: Générer une carte ==========
void renderCard(WiFiClient &c, String label, float val, String unit) {
  c.print("<div class='card'>");
  c.print("<div class='label'>" + label + "</div>");
  c.print("<div class='val'>" + String(val, 1) + "<span class='unit'>" + unit + "</span></div>");
  c.print("</div>");
}
