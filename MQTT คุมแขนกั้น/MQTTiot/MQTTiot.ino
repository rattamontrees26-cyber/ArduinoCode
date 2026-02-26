#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <vector>

const int relayPins[] = {4, 5, 13, 12, 14, 27}; 
const int relayCount = sizeof(relayPins) / sizeof(relayPins[0]);
String inputDoor = "";
boolean stringComplete = false;

// Server MQTT (HiveMQ) 
char mqtt_server[] = "297cbabd35fa4c08a5e18a2d9f27be6a.s1.eu.hivemq.cloud";
int mqtt_port = 8883;
char mqtt_user[] = "Dzmorfin";
char mqtt_pass[] = "Radazrnakub13!";
char mqtt_topic_checkserver[] = "checkserverdemo";

WiFiClientSecure espClient;
PubSubClient client(espClient);
WebServer server(80);

// (Auto-OFF)
bool relayActive[relayCount] = {false};
unsigned long relayStartTime[relayCount] = {0};
const unsigned long relay_duration = 500; // 0.5 วินาที สำหรับ MQTT

bool check_active = false;
unsigned long last_check_time = 0;
const unsigned long check_interval = 1000;

std::vector<String> mqtt_topics;
String storedSSID = "";
String storedPass = "";

#define EEPROM_SIZE 512

void openDoor(int doorIndex) {
  if (doorIndex < 0 || doorIndex >= relayCount) return;
  Serial.println("Opening Door " + String(doorIndex + 1) + "...");
  digitalWrite(relayPins[doorIndex], LOW); // สั่งเปิด (Active Low)
  delay(1000); // หน่วง 1 วินาที
  digitalWrite(relayPins[doorIndex], HIGH); // สั่งปิด
  Serial.println("Door " + String(doorIndex + 1) + " OK");
}

String scanWiFiNetworks() {
    String wifiList = "<label>เลือก WiFi:</label><select name='ssid' style='width:100%; padding:10px; margin:10px 0;'>";
    int n = WiFi.scanNetworks();
    if (n == 0) {
        wifiList += "<option value=''>ไม่พบสัญญาณ WiFi</option>";
    } else {
        for (int i = 0; i < n; i++) {
            wifiList += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
        }
    }
    wifiList += "</select>";
    return wifiList;
}

// ==========================================
//  WiFi & EEPROM
// ==========================================
void setup_wifi() {
    WiFi.mode(WIFI_STA);
    if (storedSSID != "" && storedSSID != "\xFF") {
        WiFi.begin(storedSSID.c_str(), storedPass.c_str());
        Serial.print("Connecting to WiFi: " + storedSSID);
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
            delay(1000); Serial.print("."); retries++;
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\nWiFi Fail. Entering AP Mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
        WiFi.softAP("IOT_DZMORFINSETUPWIFI", "");
    }
}

void saveSettingsToEEPROM(const String& ssid, const String& password, const String& mqtt_topic) {
    EEPROM.write(0, ssid.length());
    for (int i = 0; i < (int)ssid.length(); i++) EEPROM.write(1 + i, ssid[i]);
    int p_off = 1 + ssid.length();
    EEPROM.write(p_off, password.length());
    for (int i = 0; i < (int)password.length(); i++) EEPROM.write(p_off + 1 + i, password[i]);
    int m_off = p_off + 1 + password.length();
    EEPROM.write(m_off, mqtt_topic.length());
    for (int i = 0; i < (int)mqtt_topic.length(); i++) EEPROM.write(m_off + 1 + i, mqtt_topic[i]);
    EEPROM.commit();  
}

void loadSettingsFromEEPROM() {
    int s_len = EEPROM.read(0);
    if (s_len > 100 || s_len <= 0) return; 
    
    char ssid[s_len + 1];
    for (int i = 0; i < s_len; i++) ssid[i] = EEPROM.read(1 + i);
    ssid[s_len] = '\0';

    int p_off = 1 + s_len;
    int p_len = EEPROM.read(p_off);
    char pass[p_len + 1];
    for (int i = 0; i < p_len; i++) pass[i] = EEPROM.read(p_off + 1 + i);
    pass[p_len] = '\0';

    int m_off = p_off + 1 + p_len;
    int m_len = EEPROM.read(m_off);
    char m_top[m_len + 1];
    for (int i = 0; i < m_len; i++) m_top[i] = EEPROM.read(m_off + 1 + i);
    m_top[m_len] = '\0';

    storedSSID = String(ssid);
    storedPass = String(pass);
    mqtt_topics.clear();
    if(m_len > 0) mqtt_topics.push_back(String(m_top));
}

// ==========================================
//  (Web Server)
// ==========================================
void handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif; padding:20px;} input,select{width:100%; padding:12px; margin:8px 0; border-radius:4px; border:1px solid #ccc;}</style></head><body>";
    html += "<h1>ตั้งค่า WiFi & MQTT</h1>";
    if (WiFi.status() != WL_CONNECTED) {
        html += "<form action='/setup' method='POST'>";
        html += scanWiFiNetworks();
        html += "รหัสผ่าน WiFi: <input type='password' name='wifi_password' placeholder='ระบุรหัสผ่าน'>";
        html += "MQTT Topic: <input type='text' name='mqtt_topic' placeholder='ระบุ Topic เช่น mydoor/control'>";
        html += "<input type='submit' value='บันทึกและเชื่อมต่อ' style='background:#4CAF50; color:white; border:none; padding:15px; width:100%; cursor:pointer;'>";
        html += "</form>";
    } else {
        html += "<div style='color:green; background:#f0fff0; padding:15px; border:1px solid green;'><h3>เชื่อมต่อแล้ว</h3><p>WiFi: " + WiFi.SSID() + "</p><p>IP: " + WiFi.localIP().toString() + "</p></div>";
    }
    html += "<hr><a href='/clear'><button style='background:#f44336; color:white; border:none; padding:10px; width:100%; cursor:pointer;'>ล้างค่าทั้งหมด (Reset Device)</button></a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleSetup() {
    saveSettingsToEEPROM(server.arg("ssid"), server.arg("wifi_password"), server.arg("mqtt_topic"));
    server.send(200, "text/html", "<h2>บันทึกสำเร็จ กำลังรีสตาร์ท...</h2>");
    delay(2000); ESP.restart();
}

// ==========================================
// MQTT (Client ID จาก MAC Address)
// ==========================================
void reconnect() {
    if (!client.connected()) {
        String uniqueID = "ESP32_Door_" + WiFi.macAddress(); 
        if (client.connect(uniqueID.c_str(), mqtt_user, mqtt_pass)) {
            Serial.println("Connected to HiveMQ as: " + uniqueID);
            for (const auto& topic : mqtt_topics) client.subscribe(topic.c_str());
            client.subscribe(mqtt_topic_checkserver);
        } else {
            delay(2000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < (int)length; i++) message += (char)payload[i];
    if (message.length() == 0) return;

    Serial.println("MQTT Received [" + String(topic) + "]: " + message);
    String response = "";
    String clientId = "LaddaromElegance";

    if (strcmp(topic, mqtt_topic_checkserver) == 0) {
        if (message == "-Netstart") { check_active = true; response = "Network start " + clientId; }
        else if (message == "-Stop") { check_active = false; response = "Network stopped from " + clientId; }
        else if (message == "-Check") { response = "Server Alive from " + clientId; }
        if (response.length() > 0) client.publish(mqtt_topic_checkserver, response.c_str());
    } 
    else if (mqtt_topics.size() > 0 && strcmp(topic, mqtt_topics[0].c_str()) == 0) {
        // เปิด (ON) ส่งเลข 1-6
        if (message.length() == 1 && message[0] >= '1' && message[0] <= '6') {
            int i = message.toInt() - 1;
            digitalWrite(relayPins[i], LOW);
            relayActive[i] = true; relayStartTime[i] = millis();
            response = "Relay " + message + " ON Success";
        } 
        // ปิด (OFF) ส่งเลข 01-06
        else if (message.length() == 2 && message[0] == '0' && message[1] >= '1' && message[1] <= '6') {
            int i = String(message[1]).toInt() - 1;
            digitalWrite(relayPins[i], HIGH);
            relayActive[i] = false;
            response = "Relay " + String(i + 1) + " OFF Success";
        }
        if (response.length() > 0) client.publish(mqtt_topic_checkserver, response.c_str());
    }
}

// ==========================================
// Setup & Loop หลัก
// ==========================================
void setup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);
    loadSettingsFromEEPROM();
    inputDoor.reserve(200);

    for (int i = 0; i < relayCount; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], HIGH); // เริ่มต้นที่ OFF
    }

    setup_wifi();
    server.on("/", HTTP_GET, handleRoot);
    server.on("/setup", HTTP_POST, handleSetup);
    server.on("/clear", HTTP_GET, []() {
        for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
        EEPROM.commit();
        server.send(200, "text/html", "<h2>EEPROM Cleared. Restarting...</h2>");
        delay(1000); ESP.restart();
    });
    server.begin();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    espClient.setInsecure();
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!client.connected()) reconnect();
        client.loop();
    }
    server.handleClient();
    
    // --- ระบบ Serial (พิมพ์เลข 0-6) ---
    while (Serial.available()) {
        char inChar = (char)Serial.read();
        inputDoor += inChar;
        if (inChar == '\n') stringComplete = true;
    }
    if (stringComplete) {
        inputDoor.trim();
        if (inputDoor == "0") {
            for (int i = 0; i < relayCount; i++) openDoor(i);
        } else {
            int num = inputDoor.toInt();
            if (num >= 1 && num <= relayCount) openDoor(num - 1);
        }
        inputDoor = ""; stringComplete = false;
    }

    // --- ระบบ Auto-OFF ---
    unsigned long now = millis();
    for (int i = 0; i < relayCount; i++) {
        if (relayActive[i] && now - relayStartTime[i] >= relay_duration) {
            digitalWrite(relayPins[i], HIGH);
            relayActive[i] = false;
        }
    }

    // --- ระบบ Check Serv