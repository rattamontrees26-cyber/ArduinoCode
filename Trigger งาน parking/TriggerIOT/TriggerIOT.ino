String inputDoor = "";
boolean stringComplete = false;

// แมปค่าคำสั่ง 1–6 → พิน GPIO ของ ESP32 
// (เลือกขาที่ปลอดภัย: 13, 12, 14, 27, 26, 25)
const int relayMap[] = {13, 12, 14, 27, 26, 25};
const int mapCount = sizeof(relayMap) / sizeof(relayMap[0]);

void setup() {
  Serial.begin(115200); // ESP32 นิยมใช้ Baud rate 115200
  inputDoor.reserve(50);

  // ตั้งพินทุกตัวเป็น OUTPUT
  for (int i = 0; i < mapCount; i++) {
    pinMode(relayMap[i], OUTPUT);
    digitalWrite(relayMap[i], HIGH);
  }
  
  Serial.println("\nESP32 Ready. Enter 1-6 to trigger relay.");
}

void loop() {
  // อ่าน Serial แทนการใช้ serialEvent() เพื่อความชัวร์ใน ESP32
  handleSerial();

  if (stringComplete) {
    inputDoor.trim();
    if (inputDoor.length() > 0) {
      Serial.println("Received: " + inputDoor);

      int cmd = inputDoor.toInt();

      // ตรวจสอบว่าเป็นเลข 1–6
      if (cmd >= 1 && cmd <= mapCount) {
        int relayPin = relayMap[cmd - 1];

        Serial.print("Relay GPIO ");
        Serial.print(relayPin);
        Serial.println(" PULSE ON");
        
        digitalWrite(relayPin, LOW);
        delay(300); 
        digitalWrite(relayPin, HIGH);
        
        Serial.println("Relay OFF");
      } else {
        Serial.println("Invalid. Enter 1–6 only.");
      }
    }

    inputDoor = "";
    stringComplete = false;
  }
}

// ฟังก์ชันรับค่า Serial
void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      stringComplete = true;
    } else if (c != '\r') { // ตัด carriage return ออกถ้ามี
      inputDoor += c;
    }
  }
}