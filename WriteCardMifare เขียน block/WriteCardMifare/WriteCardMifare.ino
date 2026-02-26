#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN   21
#define RST_PIN  22

#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

#define BUZZER_PIN 27

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

byte writeData[16] = {
  0x00, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

void beepSuccess() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(300);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepError() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // --- SPI + RC522 init แบบนิ่ง ---
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  delay(50);                   

  mfrc522.PCD_Init();
  mfrc522.PCD_AntennaOn();     
  delay(100);

  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  Serial.println("RC522 Ready");
}

void loop() {

  mfrc522.PCD_StopCrypto1();

  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  Serial.println("พบบัตรแล้ว");

  byte sector = 7;
  byte blockAddr = sector * 4 + 1;
  MFRC522::StatusCode status;

  status = mfrc522.PCD_Authenticate(
             MFRC522::PICC_CMD_MF_AUTH_KEY_A,
             blockAddr,
             &key,
             &(mfrc522.uid)
           );

  if (status != MFRC522::STATUS_OK) {
    Serial.print("❌ Auth ไม่ผ่าน: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    beepError();
    goto cleanup;
  }

  status = mfrc522.MIFARE_Write(blockAddr, writeData, 16);

  if (status == MFRC522::STATUS_OK) {
    Serial.println("✅ เขียนสำเร็จ");
    beepSuccess();
  } else {
    Serial.print("❌ เขียนไม่สำเร็จ: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    beepError();
  }

cleanup:
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1000);
}