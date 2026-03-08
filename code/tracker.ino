#include <WiFi.h> 
#include <TinyGPSPlus.h> 
#include <HTTPClient.h> 
// ---------- Pin Mapping ---------- 
#define GPS_RX 16  // ESP32 RX2 <- GPS TX 
#define GPS_TX 17  // ESP32 TX2 -> GPS RX 
#define SIM_RX 4   // ESP32 GPIO4 <- SIM800 TX 
#define SIM_TX 2   // ESP32 GPIO2 -> SIM800 RX 
// ---------- WiFi ---------- 
const char* ssid     = "wifi_name"; 
const char* password = "pass"; 
// ---------- Adafruit IO ---------- 
#define AIO_USERNAME   "user1" 
#define AIO_KEY        
"enter_the_key_from_adafruit" 
const char* aio_server = "http://io.adafruit.com"; 
TinyGPSPlus gps; 
// ---------- Geofence (in METERS) ---------- 
float safeRadiusM = 10.0;   // 10 meter radius 
// home position 
float homeLat  = 0.0; 
float homeLon  = 0.0; 
bool  homeSet  = false; 
// alert state (0 = safe, 1 = out of range) 
int alert = 0; 
// To avoid SMS spam 
bool smsSent = false; 
// ---------- Helper: distance between two GPS points (km) ---------- 
float getDistance(float lat1, float lon1, float lat2, float lon2) { 
float dlat = radians(lat2 - lat1); 
float dlon = radians(lon2 - lon1);
float a = sin(dlat / 2) * sin(dlat / 2) + 
            cos(radians(lat1)) * cos(radians(lat2)) * 
            sin(dlon / 2) * sin(dlon / 2); 
  float c = 2 * atan2(sqrt(a), sqrt(1 - a)); 
  return 6371 * c;  // distance in km 
} 
 
// ---------- Helper: send SMS via SIM800 ---------- 
void sendSMSAlert(float lat, float lon) { 
  Serial.println(">>> SENDING SMS ALERT..."); 
 
  String message = "a@@ ALERT: Animal out of range!\nLat: " + 
                   String(lat, 6) + "\nLon: " + String(lon, 6); 
 
  Serial2.print("AT+CMGF=1\r"); 
  delay(1000); 
  Serial2.print("AT+CMGS=\"+911122334455\"\r");  // <-- your phone 
  delay(1000); 
  Serial2.print(message); 
  delay(500); 
  Serial2.write(26);   // CTRL+Z 
  delay(6000); 
 
  while (Serial2.available()) { 
    Serial.write(Serial2.read()); 
  } 
  Serial.println("\n       SMS alert send attempt done."); 
} 
 
// ---------- Helper: send numeric value to Adafruit IO feed ---------- 
void sendToAdafruit(const String& feed, float value) { 
  if (WiFi.status() != WL_CONNECTED) { 
    Serial.println("WiFi not connected, skipping Adafruit IO numeric"); 
    return; 
  } 
 
  HTTPClient http; 
  String url = String(aio_server) + "/api/v2/" + AIO_USERNAME + 
               "/feeds/" + feed + "/data"; 
 
  String payload = String("{\"value\":") + String(value, 6) + "}"; 
 
  http.begin(url); 
  http.addHeader("Content-Type", "application/json"); 
  http.addHeader("X-AIO-Key", AIO_KEY); 
 
  int httpCode = http.POST(payload); 
 Serial.print("Adafruit IO ["); 
  Serial.print(feed); 
  Serial.print("] HTTP status: "); 
  Serial.println(httpCode); 
 
  http.end(); 
} 
 
// ---------- Helper: send location point to 'location' feed ---------- 
void sendLocationToAdafruit(float lat, float lon) { 
  if (WiFi.status() != WL_CONNECTED) { 
    Serial.println("WiFi not connected, skipping Adafruit location"); 
    return; 
  } 
 
  HTTPClient http; 
  String url = String(aio_server) + "/api/v2/" + AIO_USERNAME + 
               "/feeds/location/data"; 
 
  String payload = String("{\"value\":1,") + 
                   "\"lat\":" + String(lat, 6) + "," + 
                   "\"lon\":" + String(lon, 6) + "}"; 
 
  http.begin(url); 
  http.addHeader("Content-Type", "application/json"); 
  http.addHeader("X-AIO-Key", AIO_KEY); 
 
  int httpCode = http.POST(payload); 
  Serial.print("Adafruit IO [location] HTTP status: "); 
  Serial.println(httpCode); 
 
  http.end(); 
} 
 
// ---------- Setup ---------- 
void setup() { 
  Serial.begin(115200); 
 
  // GPS on Serial1 (UART1) 
  Serial1.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX); 
  // GSM on Serial2 (UART2) 
  Serial2.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX); 
 
  Serial.println("Connecting to WiFi..."); 
  WiFi.begin(ssid, password); 
  while (WiFi.status() != WL_CONNECTED) { 
 delay(500); 
    Serial.print("."); 
  } 
  Serial.println("\n   WiFi connected!"); 
  Serial.print("IP Address: "); 
  Serial.println(WiFi.localIP()); 
 
  Serial.println("System ready (Adafruit only, radius in meters)..."); 
} 
 
// ---------- Main Loop ---------- 
void loop() { 
  // Feed GPS parser 
  while (Serial1.available() > 0) { 
    gps.encode(Serial1.read()); 
  } 
 
  if (gps.location.isValid()) { 
    float lat = gps.location.lat(); 
    float lon = gps.location.lng(); 
 
    // Set home position once (initial safe position) 
    if (!homeSet) { 
      homeLat = lat; 
      homeLon = lon; 
      homeSet = true; 
      Serial.println("Home position set."); 
    } 
 
    if (!homeSet) { 
      Serial.println("Waiting to set home position..."); 
      delay(1000); 
      return; 
    } 
 
    // Distance in km & meters 
    float distanceKm = getDistance(lat, lon, homeLat, homeLon); 
    float distanceM  = distanceKm * 1000.0; 
 
    // --------- SIMPLE ALERT LOGIC IN METERS --------- 
    if (distanceM > safeRadiusM) 
      alert = 1;        // outside radius → red 
    else 
      alert = 0;        // inside radius → green 
 
    Serial.println("----------------------------"); 
    Serial.print("Latitude: ");     Serial.println(lat, 6); 
 Serial.print("Longitude: ");    Serial.println(lon, 6); 
    Serial.print("Distance (m): "); Serial.println(distanceM, 1); 
    Serial.print("Alert: ");        Serial.println(alert); 
 
    // --------- SMS: only once per out-of-range event --------- 
    if (alert == 1 && !smsSent) { 
      sendSMSAlert(lat, lon); 
      smsSent = true; 
    } else if (alert == 0) { 
      smsSent = false; 
    } 
 
    // --------- Adafruit IO numeric feeds --------- 
    sendToAdafruit("latitude",  lat); 
    sendToAdafruit("longitude", lon); 
    sendToAdafruit("distance",  distanceM); 
    sendToAdafruit("alert",     alert); 
 
    // --------- Adafruit IO LOCATION feed for map --------- 
    sendLocationToAdafruit(lat, lon); 
 
    delay(20000); 
  } else { 
    Serial.println("       Waiting for GPS fix (move outdoors)..."); 
    delay(3000); 
  } 
} 
