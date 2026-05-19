DO NOT MODIFY ANYTHING IN THIS README:

![Image description](terra.png)

# 🌱 About TerraSoil

**TerraSoil** is a simplified Arduino/ESP32 library designed to turn a complex industrial sensor into an accessible monitoring tool. It was specifically developed for "10-in-1" soil sensors using the **RS485 Modbus RTU** communication protocol.

## ❓ What is TerraSoil?

Typically, reading an industrial sensor requires juggling hexadecimal codes, calculating complex checksums (CRC), and manually managing RS485 bus timings.

**TerraSoil does all of this for you.** It's a "software layer" that bridges the hardware and your application.

### What TerraSoil handles automatically:

1.  **RS485 Orchestration:** It controls the direction pin (DE/RE) so the ESP32 knows when to talk and when to listen.
2.  **Modbus Decoding:** It queries the sensor's 10 internal registers (defined in the NPKV2 manual) and translates raw bytes into real numbers (float/int).
3.  **CRC Safety:** It verifies that each piece of received data is correct. If a wire is poorly connected or there is interference, it detects it.
4.  **Data Unification:** It groups the 10 parameters (Moisture, Temperature, pH, N, P, K, EC, Salinity, TDS, Fertility) into a single, easy-to-use structure.

---

## 🎯 Why use this library?

*   **Saves Time:** What would normally take 200 lines of code is done here with **a single function**: `sensor.readSensor(data)`.
*   **Accuracy:** It integrates conversion calculations for negative temperatures and pH decimals.
*   **Compatibility:** Optimized for the **ESP32-S3** (like the Seeed Studio XIAO) but compatible with the entire ESP32 family.
*   **JSON Format:** Ideal for connected (IoT) projects, data is ready to be sent to a server or dashboard.

---

## 🔌 Operating Principle (Hardware)

TerraSoil uses **Circuit B** of the RS485 standard[LINK](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/uart.html). You need a small adapter module (TTL to RS485) between your microcontroller and the sensor.

*   **ESP32** → Sends commands (TX/RX/RTS).
*   **RS485 Adapter** → Converts signals for the sensor.
*   **TerraSoil Sensor** → Measures soil parameters via its 5 steel pins.

---

## 🚀 In summary

If you want to create an **agricultural weather station**, a **smart automatic watering system**, or a **connected soil study**, TerraSoil is the tool that makes the technical part invisible, letting you focus on your data.

![Image description](SCHEMA.png)

# 📦 INSTALLATION GUIDE - TerraSoil Library

## 🎯 Method 1: Installation via ZIP file (RECOMMENDED)

### Step 1: Download
Download the **TerraSoil.zip** file

### Step 2: Install in Arduino IDE
1. Open **Arduino IDE**
2. Menu: **Sketch** → **Include Library** → **Add .ZIP Library...**
3. Select the **TerraSoil.zip** file
4. Wait for the message: "Library added to your libraries"

### Step 3: Verify the installation
1. Menu: **File** → **Examples** → **TerraSoil**
2. You should see:
   - BasicReading
   - AdvancedReading

✅ **Installation complete!**

---

## 🎯 Method 2: Manual installation

### Locate the libraries folder

**Windows:**
```
C:\Users\[YourName]\Documents\Arduino\libraries\
```

**Mac:**
```
/Users/[YourName]/Documents/Arduino/libraries/
```

**Linux:**
```
/home/[YourName]/Arduino/libraries/
```

### Installation
1. Extract the **TerraSoil** folder from the archive
2. Copy the entire folder to the `libraries` directory
3. Restart Arduino IDE

### Expected structure
```
Arduino/
└── libraries/
    └── TerraSoil/
        ├── src/
        │   ├── TerraSoil.h
        │   └── TerraSoil.cpp
        ├── examples/
        │   ├── BasicReading/
        │   └── AdvancedReading/
        ├── library.properties
        ├── keywords.txt
        └── README.md
```

---

## 🚀 First test

### 1. Open the example
**File** → **Examples** → **TerraSoil** → **BasicReading**

### 2. Configure the pins
Check that the pins match your wiring:
```cpp
#define RS485_RX_PIN   44  // D7 on XIAO ESP32-S3
#define RS485_TX_PIN   43  // D6 on XIAO ESP32-S3
#define RS485_RTS_PIN  1   // D1 on XIAO ESP32-S3
```

### 3. Compile and upload
1. Select the board: **XIAO ESP32S3**
2. Select the COM port
3. Click **Upload** (➜)

### 4. Check the output
Open the **Serial Monitor** (115200 baud)

**Expected output:**
```json
{"hum":65.8,"temp":25.3,"ec":1250,"ph":6.5,"n":85,"p":42,"k":120,"sal":15,"tds":625,"fert":247,"ok":1,"time":5000}
```

---

## 🔧 Usage in your code

### Minimal code
```cpp
#include <TerraSoil.h>

#define RS485_RX_PIN   44
#define RS485_TX_PIN   43
#define RS485_RTS_PIN  1

HardwareSerial RS485Serial(1);
TerraSoil sensor(&RS485Serial, RS485_RTS_PIN);
TerraSoilData data;

void setup() {
  Serial.begin(115200);
  sensor.begin(RS485_RX_PIN, RS485_TX_PIN, 4800);
}

void loop() {
  if (sensor.readSensor(data)) {
    Serial.printf("Hum: %.1f%% | Temp: %.1f°C | pH: %.1f\n",
                  data.moisture, data.temperature, data.ph);
  }
  delay(5000);
}
```

---

## 📚 Available functions

### 🔷 Initialization
```cpp
sensor.begin(rxPin, txPin, baudRate)
```

### 🔷 Complete reading (MAIN)
```cpp
sensor.readSensor(data)
```
Reads all **10 parameters** in a single function!

### 🔷 Configuration
```cpp
sensor.setReadDelay(50);     // Delay between reads (ms)
sensor.setTimeout(300);      // Communication timeout (ms)
uint8_t addr = sensor.getAddress();  // Modbus address
```

### 🔷 Individual reading
```cpp
uint16_t value;
sensor.readRegister(TERRASOIL_REG_MOISTURE, value);
```

---

## 📊 Data structure

```cpp
TerraSoilData data;

// After sensor.readSensor(data) :
data.moisture      // float    - Humidity (%)
data.temperature   // float    - Temperature (°C)
data.conductivity  // uint16_t - Conductivity (µS/cm)
data.ph            // float    - pH
data.nitrogen      // uint16_t - Nitrogen (mg/kg)
data.phosphorus    // uint16_t - Phosphorus (mg/kg)
data.potassium     // uint16_t - Potassium (mg/kg)
data.salinity      // uint16_t - Salinity
data.tds           // uint16_t - TDS (mg/L)
data.fertility     // uint16_t - Fertility (mg/kg)
data.success       // bool     - true if successful
data.timestamp     // uint32_t - Timestamp (ms)
```

---

## ❓ Troubleshooting

### ❌ Error "TerraSoil.h: No such file"
**Solution:** The library is not installed correctly
- Check the libraries folder
- Restart Arduino IDE

### ❌ Error "readSensor was not declared"
**Solution:** The file does not include the library
```cpp
#include <TerraSoil.h>  // Add this line
```

### ❌ No data (timeout)
**Solution:**
1. Check RS485 connections (A to A, B to B)
2. Check sensor 5V power supply
3. Check that DE and RE are connected together

### ❌ Incorrect data
**Solution:**
1. Check common ground (all GNDs connected)
2. Check Modbus address (default 0x01)
3. Check baudrate (4800)

---

## 🔗 Usage examples

### Example 1: JSON output
```cpp
if (sensor.readSensor(data)) {
  StaticJsonDocument<256> doc;
  doc["hum"] = data.moisture;
  doc["temp"] = data.temperature;
  doc["ph"] = data.ph;
  serializeJson(doc, Serial);
}
```

### Example 2: MQTT
```cpp
if (sensor.readSensor(data)) {
  String payload = String("{\"hum\":") + data.moisture +
                   ",\"temp\":" + data.temperature + "}";
  client.publish("soil/data", payload.c_str());
}
```

### Example 3: Alert threshold
```cpp
if (sensor.readSensor(data)) {
  if (data.moisture < 30.0) {
    Serial.println("⚠️ ALERT: Soil too dry!");
  }
  if (data.ph < 6.0 || data.ph > 7.5) {
    Serial.println("⚠️ ALERT: pH out of range!");
  }
}
```

---

## 📖 Complete documentation

Check the **README.md** file in the library folder for:
- Complete API
- Modbus registers
- Detailed wiring diagrams
- More examples

---

## ✅ Installation checklist

- [ ] TerraSoil.zip file downloaded
- [ ] Library added via Arduino IDE
- [ ] Examples visible in the File → Examples menu
- [ ] Code compiles without errors
- [ ] RS485 wiring checked (A, B, DE/RE, power)
- [ ] Common ground connected
- [ ] First test successful with BasicReading

---

**🌱 Library installed! Enjoy TerraSoil! 🌱**
