#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include "DHT.h"

// --- 1. ตั้งค่า LoRa (Heltec V3) ---
// NSS: 8, DIO1: 14, NRST: 12, BUSY: 13
SX1262 radio = new Module(8, 14, 12, 13);

// --- 2. ตั้งค่า เซนเซอร์ DHT22 ---
#define DHTPIN 4          // ขา Data ของ DHT22 ต่อเข้า GPIO 4
#define DHTTYPE DHT22     // ระบุชนิดเซนเซอร์
DHT dht(DHTPIN, DHTTYPE);

// --- 3. ตั้งค่า หน้าจอ OLED (Heltec V3) ---
// ใช้ชิป SSD1306 128x64 ผ่าน I2C (SDA=17, SCL=18, RST=21)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

#define VEXT_CTRL 36      // ขาควบคุมไฟเลี้ยง Vext (จอ OLED, เซนเซอร์)
int packetCount = 0;      // ตัวแปรนับรอบการส่ง

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1. เปิดไฟเลี้ยง Vext สำหรับบอร์ด Heltec V3
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW); 
  delay(100); // รอให้ระบบไฟเสถียร

  // 2. เริ่มต้น OLED และ DHT22
  u8g2.begin();
  dht.begin();

  // แสดงข้อความต้อนรับบนจอ OLED
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "Starting Node...");
  u8g2.sendBuffer();

  // 3. เริ่มต้นระบบ LoRa ให้ตรงกับตัวรับ
  Serial.print("[LoRa] Initializing TX ... ");
  int state = radio.begin(923.2, 125.0, 7, 5, 0x34, 22, 8);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("success!");
    u8g2.drawStr(0, 30, "LoRa: Success!");
  } else {
    Serial.print("failed, code ");
    Serial.println(state);
    u8g2.drawStr(0, 30, "LoRa: Failed!");
    u8g2.sendBuffer();
    while (true); // ถ้า Error ให้หยุดการทำงาน
  }
  u8g2.sendBuffer();
  delay(1500); // หน่วงเวลาให้ผู้ใช้งานอ่านหน้าจอทัน
}

void loop() {
  // 4. อ่านค่าจากเซนเซอร์ DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // ตรวจสอบว่าอ่านค่าสำเร็จหรือไม่
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor! Skipping this cycle.");
    delay(2000); // รอ 2 วินาทีแล้วลองใหม่ (DHT22 ต้องการเวลา Refresh อย่างน้อย 2 วิ)
    return;      // ย้อนกลับไปเริ่ม loop ใหม่โดยไม่ส่งข้อมูล
  }

  packetCount++; // เพิ่มจำนวนรอบเมื่ออ่านข้อมูลสำเร็จ

  // พิมพ์ออก Serial Monitor
  Serial.print("Humidity: "); Serial.print(h); Serial.print("%  ");
  Serial.print("Temperature: "); Serial.print(t); Serial.println("°C");

  // 5. แสดงผลบนหน้าจอ OLED
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  u8g2.setCursor(0, 15); u8g2.print("Heltec V3 Node");
  u8g2.setCursor(0, 35); u8g2.print("Temp : "); u8g2.print(t, 1); u8g2.print(" C"); // โชว์ทศนิยม 1 ตำแหน่ง
  u8g2.setCursor(0, 50); u8g2.print("Humid: "); u8g2.print(h, 1); u8g2.print(" %");
  u8g2.setCursor(0, 64); u8g2.print("TX Pkg: "); u8g2.print(packetCount);
  u8g2.sendBuffer();

  // 6. สร้างข้อความ Payload สำหรับส่งผ่าน LoRa
  // ตัวอย่างข้อมูลที่ได้: "T:25.5,H:60.2,P:1"
  String payload = "T:" + String(t, 1) + ",H:" + String(h, 1) + ",P:" + String(packetCount);
  
  Serial.print("[LoRa] Transmitting: ");
  Serial.println(payload);

  // 7. สั่งส่งข้อมูล
  int state = radio.transmit(payload);

  // ตรวจสอบสถานะการส่ง
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[LoRa] Transmission successful!");
  } else {
    Serial.print("[LoRa] Error: failed, code ");
    Serial.println(state);
  }

  // 8. หน่วงเวลาก่อนส่งรอบถัดไป
  Serial.println("Waiting 5 seconds for next reading...\n");
  delay(5000); 
}