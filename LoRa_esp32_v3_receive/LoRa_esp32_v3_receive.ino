#include <WiFi.h>
#include <PubSubClient.h>
#include <RadioLib.h>

// --- ตั้งค่า WiFi ---
const char* WiFi_SSID = "KUY";
const char* WiFi_PASS = "101010101010";

// --- ตั้งค่า NETPIE MQTT ---
const char* MQTT_BROKER = "mqtt.netpie.io";
const char* MQTT_CLIENT_ID = "916989ab-e3b4-4db3-ba4e-2669c6644e60";
const char* MQTT_TOKEN = "wJAqCeYdFdzzmJqjeoY5pWZkFBS5zb5E";
const char* MQTT_SECRET = "rtc2D8xG3GFev6ANSAjeGxexce8td5bn";

// --- ตั้งค่า ฮาร์ดแวร์ ---
#define VEXT_CTRL 36    // ขาควบคุมไฟเลี้ยงบอร์ด Heltec V3 (แก้จาก 18 เป็น 36)
#define FAN_PIN 5       // ขา GPIO 5 สำหรับต่อ LED จำลองพัดลม (เปลี่ยนขาได้ตามต้องการ)
#define TEMP_THRESHOLD 30.0 // ตั้งค่าอุณหภูมิเป้าหมาย ถ้าเกินนี้ให้เปิดพัดลม

// --- ตั้งค่า LoRa (Heltec V3 - ESP32S3 + SX1262) ---
// NSS: 8, DIO1: 14, NRST: 12, BUSY: 13
SX1262 radio = new Module(8, 14, 12, 13);

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

  // 2. เปิดไฟเลี้ยง Vext สำหรับบอร์ด Heltec V3
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(100);

  // 3. เริ่มต้นระบบ LoRa ให้ตรงกับตัวส่ง
  Serial.print("[LoRa] Initializing ... ");
  int state = radio.begin(923.2, 125.0, 7, 5, 0x34, 22, 8);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("success!");
  } else {
    Serial.print("failed, code ");
    Serial.println(state);
    while (true);
  }

  // 4. เริ่มต้นระบบ WiFi และ MQTT
  setup_wifi();
  client.setServer(MQTT_BROKER, 1883);
}

void loop() {
  // รักษาการเชื่อมต่อ MQTT
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 5. รอรับสัญญาณ LoRa
  String received_str;
  int state = radio.receive(received_str);

  // หากรับสัญญาณสำเร็จ
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("\n[LoRa] Received Raw Data: ");
    Serial.println(received_str);
    
    // ดึงค่าความแรงสัญญาณ (RSSI)
    float rssi = radio.getRSSI();

    // 6. กระบวนการแยกข้อมูล (Parsing Payload: "T:25.5,H:60.2,P:1")
    float temperature = 0.0;
    float humidity = 0.0;
    int packet = 0;
    
    // หาตำแหน่งของตัวอักษรเพื่อตัดคำ
    int t_idx = received_str.indexOf("T:");
    int h_idx = received_str.indexOf(",H:");
    int p_idx = received_str.indexOf(",P:");

    // ตรวจสอบว่ารูปแบบข้อมูลถูกต้องและครบถ้วน
    if (t_idx != -1 && h_idx != -1 && p_idx != -1) {
      // ตัดสตริงและแปลงเป็นตัวเลข
      temperature = received_str.substring(t_idx + 2, h_idx).toFloat();
      humidity    = received_str.substring(h_idx + 3, p_idx).toFloat();
      packet      = received_str.substring(p_idx + 3).toInt();

      Serial.print("[Parse] Temp: "); Serial.print(temperature);
      Serial.print(" | Humid: "); Serial.print(humidity);
      Serial.print(" | Pkg: "); Serial.println(packet);

      // 7. ระบบควบคุมอัตโนมัติ (Automation Logic)
      int fan_status = 0;
      if (temperature >= TEMP_THRESHOLD) {
        digitalWrite(FAN_PIN, HIGH); // เปิดพัดลม (LED ติด)
        fan_status = 1;
        Serial.println("[Control] Temperature is HIGH! Fan: ON");
      } else {
        digitalWrite(FAN_PIN, LOW);  // ปิดพัดลม (LED ดับ)
        fan_status = 0;
        Serial.println("[Control] Temperature is OK. Fan: OFF");
      }

      // 8. จัดรูปแบบข้อมูลเป็น JSON สำหรับ NETPIE Shadow
      // แยกค่าออกเป็นตัวแปรๆ เพื่อให้ NETPIE เอาไปทำกราฟได้ง่ายขึ้น
      String payload = "{\"data\": {";
      payload += "\"Temperature\": " + String(temperature, 1) + ", ";
      payload += "\"Humidity\": " + String(humidity, 1) + ", ";
      payload += "\"FanStatus\": " + String(fan_status) + ", ";
      payload += "\"Packet\": " + String(packet) + ", ";
      payload += "\"RSSI\": " + String(rssi);
      payload += "}}";
      
      Serial.print("[MQTT] Publishing: ");
      Serial.println(payload);

      // 9. ส่งข้อมูลขึ้น NETPIE
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