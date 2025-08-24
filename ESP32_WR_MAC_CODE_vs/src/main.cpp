// ESP32 Feather MAC: XX:XX:XX:XX:XX:XX
// Отримує дані через ESP-NOW і виводить на TFT Display 1.8

// --- ESP-NOW ---
#include <esp_now.h>
#include <WiFi.h>

// --- Джойстик і кнопка ---
#include <Arduino.h>
#include <ezButton.h>

// --- TFT Display 1.8 ---
#include <SPI.h>
#include <TFT_eSPI.h>

// --- Піни джойстика ---
#define VRX_PIN  39  // X-вісь
#define VRY_PIN  36  // Y-вісь
#define BUT_PIN  4  // Кнопка

// --- Пороги для напрямків ---
#define LEFT_THRESHOLD   1000
#define RIGHT_THRESHOLD  3000
#define UP_THRESHOLD     1000
#define DOWN_THRESHOLD   3000

// --- Прототипи функцій ---
void ButSet();
void JoySet();

// --- Малювання заголовків ---
void drawEspNowHeader();
void drawGPSHeader();
void drawGasHeader();

// --- Оновлення динамічних даних ---
void updateEspNowData();
void updateGPSData();
void updateGasData();

// --- Змінні для керування екранами ---
int currentScreen = 0;   // 0 - MQ-2, 1 - GPS, 2 - Date/Time
const int totalScreens = 3; // Кількість екранів
int lastScreen = -1; // Запам’ятовування попереднього екрану

// --- Структура даних (має збігатись з передавачем) ---
typedef struct {
    char textd[32]; 
    float t;          // Температура
    float lat, lon;         // GPS координати
    float speed;            // Швидкість
    int satellites;         // К-сть супутників
    float alt;              // Висота
    int year, month, day;   // Дата
    int hour, minute, second; // Час
    float lpgMQ, coMQ, smokeMQ; // Зріджений газ, чадний газ, дим
    int sensorHumiditySoil; // Вологість грунту
} struct_message;

struct_message myData; // Буфер отриманих даних

TFT_eSPI tft = TFT_eSPI(); // Викликати бібліотеку, контакти, визначені в User_Setup.h
TFT_eSprite spr = TFT_eSprite(&tft); // Спрайт (RAM-буфер): малюємо тут, потім pushSprite() виводить на дисплей (дисплей оновлює тільки текст)
ezButton button(BUT_PIN);  // Кнопка з антидребезгом

// --- Стан джойстика ---
enum State { IDLE, LEFT, RIGHT, UP, DOWN };
State lastState = IDLE;

// --- Обробка отриманих даних ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.printf("Bytes received: %d\n", len);
  Serial.printf("Char: %s\n", myData.textd);
  Serial.printf("Temp: %.1f\n", myData.t);
  Serial.printf("Lat: %.6f\n", myData.lat);
  Serial.printf("Lon: %.6f\n", myData.lon);
  Serial.printf("Speed: %.1f\n", myData.speed);
  Serial.printf("Satellites: %d\n", myData.satellites);
  Serial.printf("Height: %.1f\n", myData.alt);
  Serial.printf("Date: %02d/%02d/%04d\n", myData.day, myData.month, myData.year);
  Serial.printf("Time: %02d:%02d:%02d\n", myData.hour, myData.minute, myData.second);
  Serial.printf("LPQ: %.1f\n", myData.lpgMQ);
  Serial.printf("CO: %.1f\n", myData.coMQ);
  Serial.printf("Smoke: %.1f\n", myData.smokeMQ);
  Serial.printf("Humidity Soil: %d%%\n", myData.sensorHumiditySoil);
  Serial.printf("Struct size: %d bytes\n\n", sizeof(myData));
}

void setup() {
  tft.init(); // Ініціалізація дисплею
  tft.setRotation(2); // Повертаємо дисплей на 90 градусів
  tft.fillScreen(TFT_BLACK); // Очищаємо екран чорним кольором
  
  spr.createSprite(tft.width(), tft.height()); // Виділяємо RAM під "віртуальний екран", на якому малюємо, а потім цілком відображаємо його на дисплей (pushSprite)

  Serial.begin(115200);

  WiFi.mode(WIFI_STA); // Режим станції

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv); // Реєстрація callback

  analogSetAttenuation(ADC_11db); // ADC до 3.3В
  button.setDebounceTime(30);     // Антидребезг кнопки
}

void loop() {
  ButSet(); // Перевірка стану кнопки
  JoySet(); // Перевірка стану джойстика

  // Малюємо новий фон та заголовок тільки при зміні екрану
  if (currentScreen != lastScreen) {
      spr.fillSprite(TFT_BLACK); // очистка всього спрайта
      lastScreen = currentScreen;
      switch(currentScreen) {
            case 0: drawEspNowHeader(); break;
            case 1: drawGPSHeader(); break;
            case 2: drawGasHeader(); break;
      }
  }

  // Постійно оновлюємо динамічний текст
  switch(currentScreen) {
        case 0: updateEspNowData(); break;
        case 1: updateGPSData(); break;
        case 2: updateGasData(); break;
  }

  spr.pushSprite(0,0); // Вивід спрайта на екран
  delay(200); // оновлення 5 разів на секунду
}

// --- Функція для обробки рухів джойстика ---
void JoySet(){
  
  int xVal = analogRead(VRX_PIN);  // Читаємо значення осі X
  int yVal = analogRead(VRY_PIN);  // Читаємо значення осі Y

  State currentState = IDLE;       // Поточний стан джойстика, за замовчуванням — IDLE
  static uint32_t tmr;             // Локальний таймер для "debounce" (затримка між спрацюваннями)

  // Пріоритет перевірки: спочатку вісь X, потім вісь Y
  if (xVal < LEFT_THRESHOLD) currentState = LEFT;       // Якщо X нижче порога — рух вліво
  else if (xVal > RIGHT_THRESHOLD) currentState = RIGHT; // Якщо X вище порога — рух вправо
  else if (yVal < UP_THRESHOLD) currentState = UP;       // Якщо Y нижче порога — рух вгору
  else if (yVal > DOWN_THRESHOLD) currentState = DOWN;   // Якщо Y вище порога — рух вниз

   if (millis() - tmr >= 150) {
        tmr = millis();
        if (currentState != lastState) {
            if (currentState == LEFT) {
                currentScreen--;
                if (currentScreen < 0) currentScreen = totalScreens-1;
            } else if (currentState == RIGHT) {
                currentScreen++;
                if (currentScreen >= totalScreens) currentScreen = 0;
            }
            lastState = currentState;
        }
    }
}


// --- Функція для обробки кнопки ---
void ButSet(){
  button.loop(); // Оновлюємо стан кнопки (потрібно викликати в кожній ітерації loop)

  if(button.isPressed()) {// Якщо кнопка була натиснута
    Serial.println("The button is pressed");
    currentScreen = 0;    // Перемикаємо на екран з датою і часом
  }                
}

// --- Вивід основної інформації ---
void drawEspNowHeader() {
  spr.fillRect(0,0,spr.width(),30,TFT_NAVY);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE,TFT_NAVY);
  spr.setTextFont(2);
  spr.drawString("ESP-NOW DATA", spr.width()/2, 15);
}

void drawGPSHeader() {
  spr.fillRect(0,0,spr.width(),30,TFT_DARKGREEN);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE,TFT_DARKGREEN);
  spr.setTextFont(2);
  spr.drawString("GPS DATA", spr.width()/2, 15);
}

void drawGasHeader() {
  spr.fillRect(0,0,spr.width(),30,TFT_MAROON);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE,TFT_MAROON);
  spr.setTextFont(2);
  spr.drawString("MQ-2 DATA", spr.width()/2, 15);
}

// --- Оновлення динамічних даних ---
void updateEspNowData() {
  spr.fillRect(0,30,spr.width(), spr.height()-30, TFT_BLACK);

  char buf[32];
  // Дата
  if (myData.year>2000) sprintf(buf,"%02d/%02d/%04d",myData.day,myData.month,myData.year);
  else sprintf(buf,"No date");
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_YELLOW,TFT_BLACK);
  spr.setTextFont(4);
  spr.drawString(buf, spr.width()/2, 60);

  // Час
  if (myData.hour>=0) sprintf(buf,"%02d:%02d:%02d",myData.hour,myData.minute,myData.second);
  else sprintf(buf,"No time");
  spr.setTextColor(TFT_CYAN,TFT_BLACK);
  spr.setTextFont(4);
  spr.drawString(buf, spr.width()/2, 95);

  // Температура
  sprintf(buf,"Temp: %.1f C",myData.t);
  spr.setTextFont(2);
  spr.setTextColor(TFT_ORANGE,TFT_BLACK);
  spr.drawString(buf, spr.width()/2, spr.height()-15);
}

void updateGPSData() {
  spr.fillRect(0,30,spr.width(), spr.height()-30, TFT_BLACK);

  char buf[40];
  spr.setTextDatum(TL_DATUM);

  //GPS
  spr.setTextColor(TFT_CYAN,TFT_BLACK);
  sprintf(buf,"Lat: %.6f",myData.lat); spr.drawString(buf,5,50);
  sprintf(buf,"Lon: %.6f",myData.lon); spr.drawString(buf,5,70);

  spr.setTextColor(TFT_YELLOW,TFT_BLACK);
  sprintf(buf,"Alt: %.1f m",myData.alt); spr.drawString(buf,5,90);

  spr.setTextColor(TFT_MAGENTA,TFT_BLACK);
  sprintf(buf,"Spd: %.1f km/h",myData.speed); spr.drawString(buf,5,110);

  spr.setTextColor(TFT_ORANGE,TFT_BLACK);
  sprintf(buf,"Sat: %d",myData.satellites); spr.drawString(buf,5,130);
}

void updateGasData() {
  spr.fillRect(0,30,spr.width(), spr.height()-30, TFT_BLACK);

  char buf[30];
  spr.setTextDatum(TL_DATUM);

  //MQ-2
  spr.setTextColor(TFT_ORANGE,TFT_BLACK);
  sprintf(buf,"CO: %.2f ppm",myData.coMQ); spr.drawString(buf,5,40);

  spr.setTextColor(TFT_RED,TFT_BLACK);
  sprintf(buf,"Smoke: %.2f ppm",myData.smokeMQ); spr.drawString(buf,5,60);
  
  spr.setTextColor(TFT_YELLOW,TFT_BLACK);
  sprintf(buf,"LPG: %.2f ppm",myData.lpgMQ); spr.drawString(buf,5,80);

  //Capacitive Soil Moisture Sensor 
  spr.setTextColor(TFT_CYAN,TFT_BLACK);
  sprintf(buf,"HumiditySoil: %d%%",myData.sensorHumiditySoil); spr.drawString(buf,5,100);
  
  //Розмір даних, які передаються
  sprintf(buf,"Struct size: %d", sizeof(myData));
  spr.setTextFont(2);
  spr.setTextColor(TFT_GREEN,TFT_BLACK);
  spr.drawString(buf, 16, 130);
}