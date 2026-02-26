#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <map>
#include <vector>

// --- Configuration ---
const char* AP_SSID = "ESP32_Config_Mode";
const char* AP_PASS = "123456789";

WebServer server(80);

struct PinConfig {
  long id;
  int pin;
  String mode; 
  String active;
};

struct RuleConfig {
  long id;
  std::vector<String> inputs;  
  std::vector<String> outputs;
};

// Global State
String wifi_ssid = "";
String wifi_pass = "";
String api_url = "";
std::vector<PinConfig> pins;
std::vector<RuleConfig> rules;

// --- Function Declarations ---
void loadConfiguration();
void applyPinModes();
void handleSave();

void setup() {
  Serial.begin(115200);
  
  if(!LittleFS.begin(true)){
    Serial.println("LittleFS Mount Failed");
    return;
  }

  loadConfiguration();

  // Setup WiFi
  if(wifi_ssid != "" && wifi_ssid != "null") {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    Serial.print("Connecting to WiFi");
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500); Serial.print("."); retries++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    } else {
      WiFi.softAP(AP_SSID, AP_PASS);
    }
  } else {
    WiFi.softAP(AP_SSID, AP_PASS);
  }

  // --- API Routes ---
  server.on("/", HTTP_GET, []() {
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  });

  server.on("/api/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    for (int i = 0; i < n; ++i) {
      JsonObject net = array.createNestedObject();
      net["ssid"] = WiFi.SSID(i);
      net["rssi"] = WiFi.RSSI(i);
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/config", HTTP_GET, []() {
    if (LittleFS.exists("/config.json")) {
      File file = LittleFS.open("/config.json", "r");
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(200, "application/json", "{}");
    }
  });

  server.on("/api/save", HTTP_POST, handleSave); // ใช้ฟังก์ชัน handleSave ที่เขียนแยกไว้ข้างล่าง

  server.on("/api/status", HTTP_GET, []() {
    DynamicJsonDocument doc(4096);
    doc["wifi"]["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi"]["ssid"] = WiFi.SSID();
    JsonArray pinStatus = doc.createNestedArray("pins");
    for(auto &p : pins) {
      JsonObject pObj = pinStatus.createNestedObject();
      pObj["pin"] = p.pin;
      pObj["val"] = digitalRead(p.pin);
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/available-pins", HTTP_GET, []() {
    int safePins[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
    int inputOnly[] = {34, 35, 36, 39};
    DynamicJsonDocument doc(1024);
    JsonArray safe = doc.createNestedArray("safe");
    for(int p : safePins) safe.add(p);
    JsonArray input = doc.createNestedArray("inputOnly");
    for(int p : inputOnly) input.add(p);
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.begin();
  applyPinModes();
}

void loop() {
  server.handleClient();
  
  // Logic Engine
  std::map<String, bool> inputStates; 
  for(auto &p : pins) {
    if(p.mode == "input") {
      int reading = digitalRead(p.pin);
      inputStates[String(p.pin)] = (p.active == "high") ? (reading == HIGH) : (reading == LOW);
    }
  }

  std::map<String, bool> desiredOutputs; 
  for(auto &r : rules) {
    bool ruleActive = !r.inputs.empty();
    for(String pinStr : r.inputs) {
      if(!inputStates[pinStr]) { ruleActive = false; break; }
    }
    if(ruleActive) {
      for(String outPin : r.outputs) desiredOutputs[outPin] = true;
    }
  }

  for(auto &p : pins) {
    if(p.mode == "output") {
      bool shouldBeActive = desiredOutputs[String(p.pin)]; 
      int hardwareLevel = (p.active == "high") ? (shouldBeActive ? HIGH : LOW) : (shouldBeActive ? LOW : HIGH);
      if(digitalRead(p.pin) != hardwareLevel) digitalWrite(p.pin, hardwareLevel);
    }
  }
}

// --- ฟังก์ชันหลักที่แก้ไข: Save โดยไม่ Restart ถ้า WiFi ไม่เปลี่ยน ---
void handleSave() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "Error"); return; }
  
  String jsonString = server.arg("plain");
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, jsonString);

  // ตรวจสอบว่า WiFi เปลี่ยนไหม
  String new_ssid = doc["wifi"]["ssid"] | "";
  String new_pass = doc["wifi"]["pass"] | "";
  bool wifiChanged = (new_ssid != wifi_ssid || new_pass != wifi_pass);

  // บันทึกไฟล์
  File file = LittleFS.open("/config.json", "w");
  if (file) { serializeJson(doc, file); file.close(); }

  // อัปเดตตัวแปร Global ทันที (Hot-Reload)
  wifi_ssid = new_ssid; wifi_pass = new_pass;
  api_url = doc["apiUrl"] | "";

  pins.clear();
  for(JsonObject p : doc["pins"].as<JsonArray>()) {
    pins.push_back({p["id"], p["pin"], p["mode"].as<String>(), p["active"].as<String>()});
  }

  rules.clear();
  for(JsonObject r : doc["rules"].as<JsonArray>()) {
    RuleConfig rc; rc.id = r["id"];
    for(String in : r["inputs"].as<JsonArray>()) rc.inputs.push_back(in);
    for(String out : r["outputs"].as<JsonArray>()) rc.outputs.push_back(out);
    rules.push_back(rc);
  }

  applyPinModes(); // บังคับใช้ค่า Pin ใหม่ทันที

  if (wifiChanged) {
    server.send(200, "application/json", "{\"status\":\"restart\"}");
    delay(1000); ESP.restart();
  } else {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  }
}

void loadConfiguration() {
  if (!LittleFS.exists("/config.json")) return;
  File file = LittleFS.open("/config.json", "r");
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, file);
  wifi_ssid = doc["wifi"]["ssid"] | "";
  wifi_pass = doc["wifi"]["pass"] | "";
  api_url = doc["apiUrl"] | "";
  pins.clear();
  for(JsonObject p : doc["pins"].as<JsonArray>()) pins.push_back({p["id"], p["pin"], p["mode"].as<String>(), p["active"].as<String>()});
  rules.clear();
  for(JsonObject r : doc["rules"].as<JsonArray>()) {
    RuleConfig rc; rc.id = r["id"];
    for(String in : r["inputs"].as<JsonArray>()) rc.inputs.push_back(in);
    for(String out : r["outputs"].as<JsonArray>()) rc.outputs.push_back(out);
    rules.push_back(rc);
  }
  file.close();
}

void applyPinModes() {
  for(auto &p : pins) {
    pinMode(p.pin, (p.mode == "output") ? OUTPUT : INPUT_PULLUP);
    if(p.mode == "output") digitalWrite(p.pin, (p.active == "high") ? LOW : HIGH);
  }
}