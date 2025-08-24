// ESP32 S3 MAC: 00:11:22:33:44:55
// Передавач

// --- ESP-NOW ---
#include <esp_now.h>
#include <WiFi.h>

// --- MCU-6050 ---
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>

// --- GPS ---
#include <TinyGPS++.h> 

// --- MQ-2 ---
#include <MQGasKit.h>

// --- Піни UART GPS ---
#define RXD2 4
#define TXD2 3

// --- Швидкість роботи GPS модуля ---
#define GPS_BAUD 9600

// --- Capacitive Soil Moisture Sensor ---
#define sensorHumPin 5

#define HUM_DRY 3700   // Значення сенсора для сухого ґрунту
#define HUM_WET 1300   // Значення сенсора для вологого ґрунту

// --- Ініціалізація об'єктів ---
TinyGPSPlus gps; //GPS
HardwareSerial gpsSerial(2);

uint8_t broadcastAddress[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; // MAC-адреса плати-приймача

Adafruit_MPU6050 mpu; // Змінна для MCU-6050

MQGasKit mq2(1, MQ2); // MQ-2 Sensor

// --- Структура для передачі даних ESP-NOW ---
typedef struct struct_message {
  char textd[32]; // Просто тестовий текст
  float t; // Температура з MPU
  float lat, lon; // GPS координати
  float speed; // Швидкість з GPS (км/год)
  int satellites; // Кількість супутників
  float alt; // Висота (метри)
  int year, month, day; // Дата
  int hour, minute, second; // Час
  float lpgMQ, coMQ, smokeMQ; // Зріджений газ, чадний газ, дим
  int sensorHumiditySoil; // Вологість грунту
} struct_message;

struct_message myData; // Змінна, куди пишемо всі дані перед відправкою

esp_now_peer_info_t peerInfo; // Інформація про "peer" (той, кому передаємо)

// --- Колбек після відправки даних ---
// Функція зворотного виклику, яка буде виконана під час надсилання повідомлення. 
// У цьому випадку ця функція просто виводить, чи було повідомлення успішно доставлено, чи ні.
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  WiFi.mode(WIFI_STA);  // Переводимо ESP32 в режим Wi-Fi станції (обов’язково для ESP-NOW)
 
  // --- Ініціалізація ESP-NOW ---
  if (esp_now_init() != ESP_OK) {
  Serial.println("Error initializing ESP-NOW");
  return;
  }

  // Реєструємо колбек для звіту про відправку
  // Після успішної ініціалізації ESP-NOW зареєструйте функцію зворотного виклику, яка буде викликана під час надсилання повідомлення. 
  // У цьому випадку ми реєструємо для OnDataSent() функція, створена раніше.
  esp_now_register_send_cb(OnDataSent);

  // Після цього нам потрібно підключитися до іншого пристрою ESP-NOW для надсилання даних.
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Додаємо отримувача у список
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  // Ініціалізація I2C для MPU6050
  Wire.begin(7, 6); // SDA = 7, SCL = 6
  Serial.println("Wire initialized");

  // Ініціалізація MPU6050
  Serial.println("MPU6050 OLED demo");
  if (!mpu.begin()) {
    Serial.println("MPU6050 init failed, continuing...");
  } else {
    Serial.println("Found MPU6050 sensor");
  }

  // Ініціалізація GPS UART
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  Serial.println("GPS Initialized");

  // Ініціалізація MQ-2
  Serial.println("Calibrating MQ-2 in clean air...");
  mq2.calibrate();
  Serial.println("Calibration complete!");
}

void loop() {
  static uint32_t tmr; // Таймер для відправки данних ESP-NOW
  static uint32_t tmr_mq2; // Таймер для MQ-2

  unsigned long start = millis();
  
  // Читаємо дані з GPS впродовж 1 секунди (щоб встигли прийти повні пакети)
  while (millis() - start < 1000) {
    while (gpsSerial.available() > 0) {
      gps.encode(gpsSerial.read()); // Парсимо байти в структуру gps
    }
  }

  // Зчитуємо дані з MPU6050
  sensors_event_t a, g, temp; 
  mpu.getEvent(&a, &g, &temp);

  // Зчитуємо дані з Capacitive Soil Moisture Sensor
  int senHumSoil = analogRead(sensorHumPin);
  myData.sensorHumiditySoil = map(senHumSoil, HUM_DRY, HUM_WET, 0, 100);

  // --- Наповнюємо структуру
  strcpy(myData.textd, "RTF TOP");

  // Дані з MPU
  myData.t = temp.temperature;

  // Дані з GPS
  myData.lat = gps.location.isValid() ? gps.location.lat() : 0.0;
  myData.lon = gps.location.isValid() ? gps.location.lng() : 0.0;
  myData.speed = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  myData.satellites = gps.satellites.isValid() ? gps.satellites.value() : -1;
  myData.alt = gps.altitude.isValid() ? gps.altitude.meters() : -1.0;
    
  // Дата з GPS 
  myData.day = gps.date.day();
  myData.month = gps.date.month();
  myData.year = gps.date.year();

  // Час з GPS (корекція +3 години під Київ)
  int correctedHour = gps.time.hour() + 3;
  if (correctedHour >= 24) correctedHour -= 24;

  myData.hour = correctedHour;
  myData.minute = gps.time.minute();
  myData.second = gps.time.second();

  
  // Дані з MQ-2
  if (millis() - tmr_mq2 >= 5000) {
    tmr_mq2 = millis();
    myData.lpgMQ = mq2.getPPM("LPG");
    myData.coMQ = mq2.getPPM("CO"); 
    myData.smokeMQ = mq2.getPPM("Smoke");
  }


  // === Відправляємо структуру по ESP-NOW ===
   if (millis() - tmr >= 1000) { // Send data every 1 second
    tmr = millis();
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

    // Перевірка успішності
      if (result == ESP_OK) {
          Serial.println("Sent with success");
      } else {
          Serial.println("Error sending the data");
      }
    }
}
