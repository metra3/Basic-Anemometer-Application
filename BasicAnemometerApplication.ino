#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_HMC5883_U.h>

#define PI 3.14159265358979323846
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD ekranın I2C adresi ve boyutları ayarlandı.

Adafruit_HMC5883_Unified mag = Adafruit_HMC5883_Unified(12345); // Manyetometre sensörü tanımlandı.

const int sensorPin = 33; // Sensör pin numarası tanımlandı.
volatile int motionCount = 0; // Hareket sayacı
volatile unsigned long lastTriggerTime = 0; // Son tetikleme zamanı
const unsigned long debounceDelay = 50; // Kararsızlık (debounce) gecikmesi
unsigned long previousMillis = 0; // Önceki milisaniye sayısı
unsigned long avgMillis = 0; // Ortalama milisaniye sayısı
unsigned long lastCountUpdate = 0; // Son sayım güncelleme zamanı
const unsigned long interval = 1000; // 1 saniyelik aralık
const unsigned long avgInterval = 10000; // 10 saniyelik aralık
const unsigned long noMotionTimeout = 2000; // Hareket yok zaman aşımı
int rpm = 0; // Devir sayısı
int rpmSum = 0; // Devir sayısı toplamı
int rpmCount = 0; // Devir sayısı sayacı
int rpmAvg = 0; // Ortalama devir sayısı

const float wheelDiameter = 0.03; // Tekerlek çapı
const float wheelCircumference = PI * wheelDiameter; // Tekerlek çevresi
float speed = 0; // Hız

const char* ssid = "Metra3"; // WiFi SSID
const char* password = "ibo123123"; // WiFi şifresi
const char* serverUrl = "https://basicanemometerapp.com.tr/data.php"; // Sunucu URL'si

void displaySensorDetails(void)
{
  sensor_t sensor;
  mag.getSensor(&sensor);
  Serial.println("------------------------------------");
  Serial.print("Sensor:       "); Serial.println(sensor.name);
  Serial.print("Driver Ver:   "); Serial.println(sensor.version);
  Serial.print("Unique ID:    "); Serial.println(sensor.sensor_id);
  Serial.print("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" uT");
  Serial.print("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" uT");
  Serial.print("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" uT");  
  Serial.println("------------------------------------");
  Serial.println("");
  delay(500);
}

// ISR (Interrupt Service Routine) fonksiyonu, sensör hareketlerini sayar.
void IRAM_ATTR isr() {
  unsigned long currentTime = millis();
  if (currentTime - lastTriggerTime > debounceDelay) {
    motionCount++;
    lastTriggerTime = currentTime;
    lastCountUpdate = currentTime;
  }
}

void setup() {
  lcd.begin(); // LCD ekran başlatıldı.
  lcd.backlight(); // LCD arka ışığı açıldı.
  
  Serial.begin(9600); // Seri haberleşme başlatıldı.
  
  Serial.println("HMC5883 Magnetometer Test");
  Serial.println("");
  
  if (!mag.begin()) { // Manyetometre sensörü başlatıldı.
    Serial.println("Ooops, no HMC5883 detected ... Check your wiring!");
    while (1);
  }
  
  displaySensorDetails(); // Sensör detayları görüntülendi.

  WiFi.begin(ssid, password); // WiFi bağlantısı başlatıldı.
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi"); // WiFi'ye bağlanıldı.

  pinMode(sensorPin, INPUT_PULLUP); // Sensör pin modu ayarlandı.
  attachInterrupt(digitalPinToInterrupt(sensorPin), isr, FALLING); // Kesme fonksiyonu ayarlandı.
  
  lcd.setCursor(0, 0);
  lcd.print("Speed (m/s):"); // LCD'de hız başlığı yazıldı.
  lcd.setCursor(0, 1);
  lcd.print("Count:"); // LCD'de sayaç başlığı yazıldı.
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - avgMillis >= avgInterval) { // Ortalama aralık zamanı kontrolü
    avgMillis = currentMillis;
    
    if (currentMillis - lastCountUpdate >= noMotionTimeout) { // Hareket yok zaman aşımı kontrolü
      rpm = 0;
    } else {
      rpm = (motionCount * 60000) / avgInterval;
    }

    sensors_event_t event; 
    mag.getEvent(&event);

    Serial.print("X: "); Serial.print(event.magnetic.x); Serial.print("  ");
    Serial.print("Y: "); Serial.print(event.magnetic.y); Serial.print("  ");
    Serial.print("Z: "); Serial.print(event.magnetic.z); Serial.print("  ");

    float heading = atan2(event.magnetic.y, event.magnetic.x);
    float declinationAngle = 0.22; // Manyetik sapma açısı
    heading += declinationAngle;
  
    if (heading < 0)
      heading += 2 * PI;
    if (heading > 2 * PI)
      heading -= 2 * PI;
   
    float headingDegrees = heading * 180 / M_PI; // Başlık açısı dereceye çevrildi.
  
    Serial.print("Heading (degrees): "); Serial.print(headingDegrees);
    Serial.print(" Yön: ");
    String direction = getDirection(headingDegrees); // Yön belirleme
    Serial.println(direction);

    speed = (wheelCircumference * rpm) / 60;  // Hız hesaplama
    lcd.setCursor(0, 1);
    lcd.print("         ");
    lcd.setCursor(0, 1);
    lcd.print(speed, 2); // Hız LCD'ye yazıldı.
    
    motionCount = 0; // Hareket sayacı sıfırlandı.
    
    Serial.print("RPM: ");
    Serial.print(rpm);
    Serial.print(" Speed: ");
    Serial.print(speed, 2);
    Serial.println(" m/s");
    
    if (WiFi.status() == WL_CONNECTED) { // WiFi bağlantısı kontrolü
      HTTPClient http;
      http.begin(serverUrl); // Sunucuya bağlantı başlatıldı.
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Accept", "application/json");
      
      // Post verisi oluşturuldu.
      String postData = "{\"rpm\": \"" + String(rpm) + "\", \"speed\": \"" + String(speed, 2) + "\", \"direction\": \"" + direction + "\", \"headingDegrees\": \"" + String(headingDegrees, 2) + "\"}";
      
      int httpResponseCode = http.POST(postData); // Post verisi gönderildi.
      
      if (httpResponseCode > 0) { // Yanıt kodu kontrolü
        String response = http.getString();
        Serial.print("HTTP Yanıt Kodu: ");
        Serial.println(httpResponseCode);
        Serial.print("Sunucu Yanıtı: ");
        Serial.println(response);
      } else {
        Serial.print("POST Gönderirken Hata: ");
        Serial.println(httpResponseCode);
      }
      
      http.end(); // HTTP bağlantısı sonlandırıldı.
    } else {
      Serial.println("WiFi Bağlı Değil");
    }
  }

  lcd.setCursor(8, 1);
  lcd.print("      ");
  lcd.setCursor(8, 1);
  lcd.print(motionCount); // Hareket sayısı LCD'ye yazıldı.
  
  delay(100);
}

// Yön belirleme fonksiyonu
String getDirection(float heading) {
  if (heading > 22.5 && heading <= 67.5) {
    return "Kuzeydoğu";
  } else if (heading > 67.5 && heading <= 112.5) {
    return "Doğu";
  } else if (heading > 112.5 && heading <= 157.5) {
    return "Güneydoğu";
  } else if (heading > 157.5 && heading <= 202.5) {
    return "Güney";
  } else if (heading > 202.5 && heading <= 247.5) {
    return "Güneybatı";
  } else if (heading > 247.5 && heading <= 292.5) {
    return "Batı";
  } else if (heading > 292.5 && heading <= 337.5) {
    return "Kuzeybatı";
  } else {
    return "Kuzey";
  }
}
