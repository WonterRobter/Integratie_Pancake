#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

// ====== NETWERK ======
#include <WiFiS3.h>      // UNO R4 WiFi
#include <ArduinoJson.h> // JSON body bouwen

#include "secrets.h" // SSID, PASSWORD, FLASK_HOST, FLASK_PORT
#include "config.h"  // POST_INTERVAL_MS

// --- DESIGN KLEUREN ---
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREY 0x5AEB
#define C_HEADER 0x001F // Donkerblauw
#define C_SELECT 0xFD20 // Oranje/Goud
#define C_BG 0x10A2     // Zeer donker grijs
#define C_GREEN 0x07E0
#define C_RED 0xF800

// --- INSTELLINGEN ---
const int ITEMS_PER_PAGE = 4;

// --- PIN LAYOUT ---
const int encPinA = 3;
const int encPinB = 5;
const int encBtn = 6;
#define TFT_CS 10
#define TFT_RST 9
#define TFT_DC 8
const int relayPin = 7;

// --- OBJECTEN ---
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_MLX90632 mlx = Adafruit_MLX90632();
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);

// NETWERK
WiFiClient wifiClient;

// --- MENU DATA ---
struct Programma
{
  String naam;
  int temp;
};

Programma menus[] = {
    {"Handwarm", 30},
    {"Slow Cook", 32},
    {"Thee Water", 35},
    {"Koken", 100},
    {"Braden", 140},
    {"Pasta", 100},
    {"Soep", 90},
    {"Chocolade", 45},
    {"Warmhouden", 50},
    {"Boost Mode", 120},
    {"Sous Vide", 56},
    {"Koffie", 92}};

unsigned long previousLcdMillis = 0;
const long lcdInterval = 500;

int menuCount = sizeof(menus) / sizeof(menus[0]);
int menuIndex = 0;
int lastEncState;
bool isActive = false;
int targetTemp = 0;
uint16_t lastTFTColor = C_SELECT;

// POST interval
unsigned long lastPostMillis = 0;

// sessie id die we van de server krijgen
int currentSessionId = -1;
int currentProgramId = 0;

// --- FUNCTIES ---
void drawMenuPage();
void updateMenuItem(int index, bool selected);
void drawStaticInfoScreen();
void updateTFTColor(double currentTemp);

// ========= NETWERKFUNCTIES =========

void connectWiFi()
{
  Serial.print("Verbinden met WiFi: ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("WiFi verbonden, IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi verbinden mislukt.");
  }
}

String readHttpResponse(WiFiClient &client)
{
  String response;
  unsigned long start = millis();
  while (client.connected() && millis() - start < 2000)
  {
    while (client.available())
    {
      char c = client.read();
      response += c;
    }
  }
  return response;
}

int startSessionOnServer(int programId)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Geen WiFi, kan geen sessie starten.");
      return -1;
    }
  }

  StaticJsonDocument<128> doc;
  doc["program_id"] = programId;

  String body;
  serializeJson(doc, body);

  Serial.println("POST naar Flask /api/start_session:");
  Serial.println(body);

  if (!wifiClient.connect(FLASK_HOST, FLASK_PORT))
  {
    Serial.println("Verbinding met Flask server mislukt (start_session).");
    return -1;
  }

  wifiClient.println("POST /api/start_session HTTP/1.1");
  wifiClient.print("Host: ");
  wifiClient.println(FLASK_HOST);
  wifiClient.println("Content-Type: application/json");
  wifiClient.print("Content-Length: ");
  wifiClient.println(body.length());
  wifiClient.println("Connection: close");
  wifiClient.println();
  wifiClient.print(body);

  String response = readHttpResponse(wifiClient);
  wifiClient.stop();
  Serial.println("RESP start_session:");
  Serial.println(response);

  int bodyIndex = response.indexOf("\r\n\r\n");
  String respBody = (bodyIndex >= 0) ? response.substring(bodyIndex + 4) : response;
  respBody.trim();
  Serial.println("BODY start_session:");
  Serial.println(respBody);

  int idx = respBody.indexOf("session_id");
  if (idx < 0)
  {
    Serial.println("Kon session_id niet vinden in body.");
    return -1;
  }
  int colon = respBody.indexOf(":", idx);
  if (colon < 0)
    return -1;
  int end = respBody.indexOf("}", colon);
  if (end < 0)
    end = respBody.length();

  String idStr = respBody.substring(colon + 1, end);
  idStr.trim();
  int sid = idStr.toInt();
  if (sid <= 0)
  {
    Serial.println("Ongeldige session_id uit body.");
    return -1;
  }

  return sid;
}

/*
 * Stuurt één meting naar Flask.
 * JSON: { temperature, program_id, session_id, status, timestamp }
 * status = 'preheat' | 'cook' | 'flip' | 'wait' | 'stop'
 */
void postSensorData(double temp, const char *statusStr)
{
  if (currentSessionId <= 0)
  {
    Serial.println("Geen geldige session_id, POST overgeslagen.");
    return;
  }

  if (isnan(temp))
  {
    Serial.println("Temp is NaN, POST overgeslagen.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Geen WiFi, POST overgeslagen.");
      return;
    }
  }

  StaticJsonDocument<256> doc;
  doc["temperature"] = temp;
  doc["program_id"] = currentProgramId;
  doc["session_id"] = currentSessionId;
  doc["status"] = statusStr;
  doc["timestamp"] = millis(); // server gebruikt zelf NOW()

  String body;
  serializeJson(doc, body);

  Serial.println("POST naar Flask /api/sensor_data:");
  Serial.println(body);

  if (!wifiClient.connect(FLASK_HOST, FLASK_PORT))
  {
    Serial.println("Verbinding met Flask server mislukt.");
    return;
  }

  wifiClient.println("POST /api/sensor_data HTTP/1.1");
  wifiClient.print("Host: ");
  wifiClient.println(FLASK_HOST);
  wifiClient.println("Content-Type: application/json");
  wifiClient.print("Content-Length: ");
  wifiClient.println(body.length());
  wifiClient.println("Connection: close");
  wifiClient.println();
  wifiClient.print(body);

  String resp = readHttpResponse(wifiClient);
  wifiClient.stop();
  Serial.println("RESP sensor_data:");
  Serial.println(resp);

  Serial.println("POST klaar.");
}

// ========= SETUP =========

void setup()
{
  Serial.begin(115200);

  // Hardware Setup
  pinMode(encPinA, INPUT_PULLUP);
  pinMode(encPinB, INPUT_PULLUP);
  pinMode(encBtn, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  lastEncState = digitalRead(encPinA);

  // LCD & Sensor Init
  lcd.init();
  lcd.setRGB(0, 0, 255);
  if (!mlx.begin())
  {
    lcd.setRGB(255, 0, 0);
    lcd.print("Sensor Fout");
    while (1)
      ;
  }
  mlx.setMode(MLX90632_MODE_CONTINUOUS);

  // TFT Init
  tft.init(170, 320);
  tft.setRotation(3);
  tft.fillScreen(C_BG);

  // WiFi
  connectWiFi();

  drawMenuPage();
}

// ========= LOOP =========

void loop()
{
  // 1. NAVIGATIE
  if (!isActive)
  {
    int currentEncState = digitalRead(encPinA);

    if (currentEncState != lastEncState && currentEncState == LOW)
    {
      int oldIndex = menuIndex;
      int oldPage = oldIndex / ITEMS_PER_PAGE;

      if (digitalRead(encPinB) != currentEncState)
        menuIndex++;
      else
        menuIndex--;

      if (menuIndex >= menuCount)
        menuIndex = 0;
      if (menuIndex < 0)
        menuIndex = menuCount - 1;

      int newPage = menuIndex / ITEMS_PER_PAGE;

      if (newPage != oldPage)
      {
        drawMenuPage();
      }
      else
      {
        updateMenuItem(oldIndex, false);
        updateMenuItem(menuIndex, true);
      }
    }
    lastEncState = currentEncState;
  }

  // 2. KNOP START/STOP
  if (digitalRead(encBtn) == LOW)
  {
    delay(50);
    while (digitalRead(encBtn) == LOW)
      ;
    delay(50);

    isActive = !isActive;

    if (isActive)
    {
      // START
      targetTemp = menus[menuIndex].temp;
      currentProgramId = menuIndex + 1; // eenvoudige mapping
      lastTFTColor = C_SELECT;
      drawStaticInfoScreen();

      currentSessionId = startSessionOnServer(currentProgramId);
      if (currentSessionId <= 0)
      {
        Serial.println("Sessie starten faalde, stoppen.");
        isActive = false;
        digitalWrite(relayPin, LOW);
        lcd.setRGB(255, 0, 0);
        lcd.clear();
        lcd.print("Sessiefout");
        tft.fillScreen(C_BG);
        drawMenuPage();
        return;
      }

      double temp = mlx.getObjectTemperature();
      if (!isnan(temp))
      {
        postSensorData(temp, "preheat");
      }
    }
    else
    {
      // STOP
      double temp = mlx.getObjectTemperature();
      digitalWrite(relayPin, LOW);
      lcd.setRGB(0, 0, 255);
      lcd.clear();
      lcd.print("Gestopt");

      if (!isnan(temp))
      {
        postSensorData(temp, "stop");
      }
      currentSessionId = -1;

      tft.fillScreen(C_BG);
      drawMenuPage();
    }
  }

  // 3. PROCES (verwarmen)
  if (isActive)
  {
    double temp = mlx.getObjectTemperature();
    if (isnan(temp))
    {
      Serial.println("MLX gaf NaN, skip cycle");
      return;
    }

    // Relais sturing
    if (temp < targetTemp)
      digitalWrite(relayPin, HIGH);
    else
      digitalWrite(relayPin, LOW);

    updateTFTColor(temp);

    unsigned long currentMillis = millis();

    // LCD‑update
    if (currentMillis - previousLcdMillis >= lcdInterval)
    {
      previousLcdMillis = currentMillis;

      // regel 0 volledig wissen
      lcd.setCursor(0, 0);
      lcd.print("                ");

      if (temp > (targetTemp + 5))
      {
        lcd.setRGB(255, 0, 0);
        lcd.setCursor(0, 0);
        lcd.print("!! TE HEET !!   ");
      }
      else if (temp < (targetTemp - 2))
      {
        lcd.setRGB(255, 100, 0);
        lcd.setCursor(0, 0);
        lcd.print("Opwarmen...     ");
      }
      else
      {
        lcd.setRGB(0, 255, 0);
        lcd.setCursor(0, 0);
        lcd.print("Stabiel         ");
      }

      // regel 1 volledig wissen
      lcd.setCursor(0, 1);
      lcd.print("                ");

      lcd.setCursor(0, 1);
      lcd.print("Nu:");
      lcd.print(temp, 1);
      lcd.print((char)223);
      lcd.print(" / ");
      lcd.print(targetTemp);
    }

    // ====== STATUS VOOR DB OP BASIS VAN TEMP ======
    const double tooHot = targetTemp + 5;
    const double tooCold = targetTemp - 2;
    const char *statusForDb;

    if (temp > tooHot)
      statusForDb = "wait";
    else if (temp < tooCold)
      statusForDb = "preheat";
    else
      statusForDb = "cook";

    // PERIODIEKE POST NAAR FLASK
    if (currentMillis - lastPostMillis >= POST_INTERVAL_MS)
    {
      lastPostMillis = currentMillis;
      postSensorData(temp, statusForDb);
    }
  }
}

// ==========================================
// GRAFISCHE FUNCTIES
// ==========================================

void drawMenuPage()
{
  tft.fillRect(0, 0, 320, 40, C_HEADER);
  tft.drawFastHLine(0, 40, 320, C_WHITE);
  tft.setCursor(60, 10);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.print("CHEF MENU");
  tft.fillRect(0, 41, 320, 130, C_BG);

  int currentPage = menuIndex / ITEMS_PER_PAGE;
  int startIdx = currentPage * ITEMS_PER_PAGE;

  for (int i = 0; i < ITEMS_PER_PAGE; i++)
  {
    int realIndex = startIdx + i;
    if (realIndex < menuCount)
    {
      updateMenuItem(realIndex, (realIndex == menuIndex));
    }
  }
}

void updateMenuItem(int index, bool selected)
{
  int currentPage = menuIndex / ITEMS_PER_PAGE;
  int itemPage = index / ITEMS_PER_PAGE;
  if (currentPage != itemPage)
    return;

  int relativePos = index % ITEMS_PER_PAGE;
  int yPos = 55 + (relativePos * 30);

  if (selected)
  {
    tft.fillRoundRect(10, yPos, 300, 26, 5, C_SELECT);
    tft.setTextColor(C_BLACK);
  }
  else
  {
    tft.fillRoundRect(10, yPos, 300, 26, 5, C_BG);
    tft.setTextColor(C_WHITE);
  }
  tft.setCursor(20, yPos + 4);
  tft.setTextSize(2);
  tft.print(menus[index].naam);
  tft.setCursor(240, yPos + 4);
  tft.print(menus[index].temp);
  tft.print("C");
}

void drawStaticInfoScreen()
{
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 50, C_HEADER);
  tft.setCursor(10, 15);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.print(menus[menuIndex].naam);

  tft.fillRoundRect(20, 65, 280, 80, 10, C_BLACK);
  tft.drawRoundRect(20, 65, 280, 80, 10, C_GREY);

  tft.setCursor(40, 75);
  tft.setTextColor(C_GREY);
  tft.setTextSize(2);
  tft.print("Ingesteld op:");

  tft.setCursor(120, 100);
  tft.setTextColor(C_SELECT);
  tft.setTextSize(4);
  tft.print(targetTemp);
  tft.print("C");

  tft.setCursor(15, 155);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(1);
  tft.print("-> Zie kleine scherm voor live status");
}

void updateTFTColor(double currentTemp)
{
  uint16_t newColor;

  if (currentTemp > (targetTemp + 5))
  {
    newColor = C_RED;
  }
  else if (currentTemp < (targetTemp - 2))
  {
    newColor = C_SELECT;
  }
  else
  {
    newColor = C_GREEN;
  }

  static double lastTempDrawn = -1000;

  if (newColor != lastTFTColor || fabs(currentTemp - lastTempDrawn) >= 0.1)
  {
    tft.fillRect(115, 95, 120, 40, C_BLACK);
    tft.setCursor(120, 100);
    tft.setTextColor(newColor);
    tft.setTextSize(4);
    tft.print(currentTemp, 1);
    tft.print("C");
    lastTFTColor = newColor;
    lastTempDrawn = currentTemp;
  }
}
