#include <WiFi.h>
#include <PubSubClient.h>
#include <RadioLib.h>
#include <U8g2lib.h> // เพิ่มไลบรารีสำหรับหน้าจอ OLED

// --- ตั้งค่า WiFi ---
const char* WiFi_SSID = "Smart";
const char* WiFi_PASS = "0123456789";

// --- ตั้งค่า NETPIE MQTT ---
const char* MQTT_BROKER = "mqtt.netpie.io";
const char* MQTT_CLIENT_ID = "916989ab-e3b4-4db3-ba4e-2669c6644e60";
const char* MQTT_TOKEN = "wJAqCeYdFdzzmJqjeoY5pWZkFBS5zb5E";
const char* MQTT_SECRET = "rtc2D8xG3GFev6ANSAjeGxexce8td5bn";

// --- ตั้งค่า ฮาร์ดแวร์ ---
#define VEXT_CTRL 36    // ขาควบคุมไฟเลี้ยงบอร์ด Heltec V3
#define FAN_PIN 5       // ขา GPIO 5 สำหรับต่อ LED จำลองพัดลม
#define TEMP_THRESHOLD 20.0 // ตั้งค่าอุณหภูมิเป้าหมาย ถ้าเกินนี้ให้เปิดพัดลม

// --- ตั้งค่า LoRa (Heltec V3) ---
// NSS: 8, DIO1: 14, NRST: 12, BUSY: 13
SX1262 radio = new Module(8, 14, 12, 13);

// --- ตั้งค่า หน้าจอ OLED (Heltec V3) ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.print("WiFi connecting to ");
  Serial.println(WiFi_SSID);
  
  WiFi.begin(WiFi_SSID, WiFi_PASS);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.print(" connected to ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(MQTT_CLIENT_ID, MQTT_TOKEN, MQTT_SECRET)) {
      Serial.println("MQTT Connected");
    } else {
      Serial.print("MQTT Error, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1. ตั้งค่าขาพัดลม (LED) เป็น Output
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW); // ปิดพัดลมเริ่มต้น

  // 2. เปิดไฟเลี้ยง Vext สำหรับบอร์ด Heltec V3 (รวมถึงหน้าจอ OLED)
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);

  // 3. เริ่มต้นหน้าจอ OLED ฝั่งตัวรับ
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, "Receiver Starting...");
  u8g2.sendBuffer();

  // 4. เริ่มต้นระบบ LoRa ให้ตรงกับตัวส่ง
  Serial.print("[LoRa] Initializing ... ");
  int state = radio.begin(923.2, 125.0, 7, 5, 0x34, 22, 8);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("success!");
    u8g2.drawStr(0, 35, "LoRa: Ready!");
  } else {
    Serial.print("failed, code ");
    Serial.println(state);
    u8g2.drawStr(0, 35, "LoRa: Failed!");
    u8g2.sendBuffer();
    while (true);
  }
  u8g2.sendBuffer();

  // 5. เริ่มต้นระบบ WiFi และ MQTT
  setup_wifi();
  client.setServer(MQTT_BROKER, 1883);

  // แสดงสถานะว่าเชื่อมต่อ WiFi เสร็จแล้ว
  u8g2.drawStr(0, 55, "WiFi: Connected!");
  u8g2.sendBuffer();
  delay(1000); // หน่วงเวลาให้ผู้ใช้งานอ่านทัน
}

void loop() {
  // รักษาการเชื่อมต่อ MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 6. รอรับสัญญาณ LoRa
  String received_str;
  int state = radio.receive(received_str);

  // หากรับสัญญาณสำเร็จ
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("\n[LoRa] Received Raw Data: ");
    Serial.println(received_str);
    
    // ดึงค่าความแรงสัญญาณ (RSSI)
    float rssi = radio.getRSSI();

    // 7. กระบวนการแยกข้อมูล (Parsing Payload: "T:25.5,H:60.2,P:1")
    float temperature = 0.0;
    float humidity = 0.0;
    int packet = 0;
    
    int t_idx = received_str.indexOf("T:");
    int h_idx = received_str.indexOf(",H:");
    int p_idx = received_str.indexOf(",P:");

    if (t_idx != -1 && h_idx != -1 && p_idx != -1) {
      temperature = received_str.substring(t_idx + 2, h_idx).toFloat();
      humidity    = received_str.substring(h_idx + 3, p_idx).toFloat();
      packet      = received_str.substring(p_idx + 3).toInt();

      // 8. ระบบควบคุมอัตโนมัติ (Automation Logic)
      int fan_status = 0;
      if (temperature >= TEMP_THRESHOLD) {
        digitalWrite(FAN_PIN, HIGH); // เปิดพัดลม (LED ติด)
        fan_status = 1;
      } else {
        digitalWrite(FAN_PIN, LOW);  // ปิดพัดลม (LED ดับ)
        fan_status = 0;
      }

      // --- 9. อัปเดตข้อมูลขึ้นหน้าจอ OLED ฝั่งตัวรับ ---
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.setCursor(0, 15); u8g2.print("RX Node (Center)");
      
      u8g2.setCursor(0, 32); 
      u8g2.print("T: "); u8g2.print(temperature, 1); u8g2.print("C  ");
      u8g2.print("H: "); u8g2.print(humidity, 1); u8g2.print("%");
      
      u8g2.setCursor(0, 48); 
      u8g2.print("Fan: "); 
      if(fan_status == 1) u8g2.print("ON"); else u8g2.print("OFF");
      
      u8g2.setCursor(0, 64); 
      u8g2.print("RSSI: "); u8g2.print((int)rssi); u8g2.print(" dBm");
      u8g2.sendBuffer();
      // ------------------------------------------------

      // 10. จัดรูปแบบข้อมูลเป็น JSON สำหรับ NETPIE Shadow
      String payload = "{\"data\": {";
      payload += "\"Temperature\": " + String(temperature, 1) + ", ";
      payload += "\"Humidity\": " + String(humidity, 1) + ", ";
      payload += "\"FanStatus\": " + String(fan_status) + ", ";
      payload += "\"Packet\": " + String(packet) + ", ";
      payload += "\"RSSI\": " + String(rssi);
      payload += "}}";
      
      Serial.print("[MQTT] Publishing: ");
      Serial.println(payload);

      // 11. ส่งข้อมูลขึ้น NETPIE
      client.publish("@shadow/data/update", payload.c_str());
      
    } else {
      Serial.println("[Error] Payload format is incorrect!");
    }
  } 
  else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.print("[LoRa] Receive failed, code ");
    Serial.println(state);
  }
}
