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

// ====== BRONNEN ======
// Zie https://github.com/WonterRobter/Integratie_Pancake/ voor bronnenlijst

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
  int dbId;      // program_id uit de DB
  int flipTime;  // flip-tijd (s)
  int totalTime; // totale tijd (s)
};

Programma menus[12];
int menuCount = 0;

// --- STATE VARIABELEN ---
unsigned long previousLcdMillis = 0;
const long lcdInterval = 500;

unsigned long previousTftMillis = 0;
const long tftInterval = 1000; // TFT max 1x/s updaten

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

// FLIP-STATE (max 1 flip per sessie)
bool flipTimerActive = false;
unsigned long flipStartMillis = 0;
int currentFlipTime = 0; // in seconden
bool flipWaitingAck = false;
bool flipAlreadyDone = false;

// SECOND-FASE (na flip tot einde totale tijd)
bool totalTimerActive = false;
unsigned long totalStartMillis = 0;
int currentTotalTime = 0;     // totale baktijd in seconden
bool totalWaitingAck = false; // wachten op knop na einde totale tijd

// extra flag om direct te tekenen na events (bv. knop)
bool forceImmediateTftDraw = false;

// --- FUNCTIEDECLARATIES ---
void drawMenuPage();
void updateMenuItem(int index, bool selected);
void drawStaticInfoScreen();
void updateTFTColor(double currentTemp);
void drawTftFrame(double currentTemp, const char *statusForDb); // centrale TFT-tekening
String readHttpResponse(WiFiClient &client);
void connectWiFi();
void loadProgramsFromServer();
int startSessionOnServer(int programId);
void postSensorData(double temp, const char *statusStr);

// Simuleer hogere temperatuur 21–150 °C op basis van echte sensor 21–35 °C
double getVirtualTemp(double rawTemp)
{
  const double room = 21.0;
  const double maxRaw = 35.0;
  const double maxVirt = 150.0;

  if (rawTemp <= room)
    return rawTemp;
  if (rawTemp >= maxRaw)
    return maxVirt;

  double frac = (rawTemp - room) / (maxRaw - room); // 0..1
  return room + frac * (maxVirt - room);
}

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

void loadProgramsFromServer()
{
  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (!wifiClient.connect(FLASK_HOST, FLASK_PORT))
    return;

  wifiClient.println("GET /api/programs HTTP/1.1");
  wifiClient.print("Host: ");
  wifiClient.println(FLASK_HOST);
  wifiClient.println("Connection: close");
  wifiClient.println();

  String resp = readHttpResponse(wifiClient);
  wifiClient.stop();

  int bodyIndex = resp.indexOf("\r\n\r\n");
  String body = (bodyIndex >= 0) ? resp.substring(bodyIndex + 4) : resp;
  body.trim();

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    Serial.println("JSON parse error /api/programs");
    return;
  }

  menuCount = 0;
  for (JsonObject p : doc.as<JsonArray>())
  {
    if (menuCount >= 12)
      break;
    menus[menuCount].naam = (const char *)p["name"];
    menus[menuCount].temp = (int)p["target_temp"];
    menus[menuCount].dbId = (int)p["program_id"];
    menus[menuCount].flipTime = (int)p["flip_time"];
    menus[menuCount].totalTime = (int)p["total_time"];
    menuCount++;
  }

  Serial.print("Programma's geladen: ");
  Serial.println(menuCount);

  // extra menu-item voor refresh onderaan
  if (menuCount < 12)
  {
    menus[menuCount].naam = "REFRESH PROGRAMMA'S";
    menus[menuCount].temp = 0;
    menus[menuCount].dbId = -1; // speciale ID
    menus[menuCount].flipTime = 0;
    menus[menuCount].totalTime = 0;
    menuCount++;
  }
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
  doc["temperature"] = temp; // hier log je de echte sensorwaarde (raw)
  doc["program_id"] = currentProgramId;
  doc["session_id"] = currentSessionId;
  doc["status"] = statusStr;
  doc["timestamp"] = millis();

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

  pinMode(encPinA, INPUT_PULLUP);
  pinMode(encPinB, INPUT_PULLUP);
  pinMode(encBtn, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  lastEncState = digitalRead(encPinA);

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

  tft.init(170, 320);
  tft.setRotation(3);
  tft.fillScreen(C_BG);

  connectWiFi();
  loadProgramsFromServer();
  drawMenuPage();
}

// ========= LOOP =========

void loop()
{
  // 1. FLIP-BEVESTIGING
  if (flipWaitingAck && digitalRead(encBtn) == LOW)
  {
    delay(50);
    while (digitalRead(encBtn) == LOW)
      ;
    delay(50);

    flipWaitingAck = false;
    flipTimerActive = false;
    flipAlreadyDone = true;

    // nu start de tweede fase (totale tijd - flip)
    if (currentTotalTime > currentFlipTime)
    {
      totalTimerActive = true;
      totalStartMillis = millis();
      totalWaitingAck = false;
    }

    tft.fillRect(20, 65, 280, 80, C_BLACK); // nu middenvak leegmaken
    tft.fillRect(0, 200, 320, 40, C_BG);    // onderbalk leeg
    lcd.setRGB(0, 255, 0);
    lcd.setCursor(0, 0);
    lcd.print("Na flip, bak... ");
    forceImmediateTftDraw = true; // nu meteen scherm updaten

    double raw = mlx.getObjectTemperature();
    if (!isnan(raw))
    {
      postSensorData(raw, "flip");
    }

    return;
  }

  // 2. EINDE TOTALE TIJD - WACHTEN OP KNOP
  if (totalWaitingAck && digitalRead(encBtn) == LOW)
  {
    delay(50);
    while (digitalRead(encBtn) == LOW)
      ;
    delay(50);

    double raw = mlx.getObjectTemperature();
    if (!isnan(raw))
    {
      postSensorData(raw, "stop");
    }

    isActive = false;
    currentSessionId = -1;
    totalWaitingAck = false;
    totalTimerActive = false;

    digitalWrite(relayPin, LOW);
    lcd.setRGB(0, 0, 255);
    lcd.clear();
    lcd.print("Klaar!");

    tft.fillScreen(C_BG);
    drawMenuPage();

    return;
  }

  // 3. NAVIGATIE
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
        drawMenuPage(); // nu volledige menupagina opnieuw tekenen
      }
      else
      {
        updateMenuItem(oldIndex, false);
        updateMenuItem(menuIndex, true);
      }
    }
    lastEncState = currentEncState;
  }

  // 4. START/STOP (enkel als we niet in een wacht-ack zitten)
  if (!flipWaitingAck && !totalWaitingAck && digitalRead(encBtn) == LOW)
  {
    delay(50);
    while (digitalRead(encBtn) == LOW)
      ;
    delay(50);

    isActive = !isActive;

    if (isActive)
    {
      // speciale case: REFRESH PROGRAMMA'S
      if (menus[menuIndex].dbId == -1)
      {
        isActive = false;
        loadProgramsFromServer(); // nu programma's opnieuw ophalen
        drawMenuPage();
        return;
      }

      targetTemp = menus[menuIndex].temp;
      currentProgramId = menus[menuIndex].dbId;
      currentFlipTime = menus[menuIndex].flipTime;
      currentTotalTime = menus[menuIndex].totalTime;
      lastTFTColor = C_SELECT;
      drawStaticInfoScreen(); // nu info-scherm tekenen

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

      double raw = mlx.getObjectTemperature();
      if (!isnan(raw))
      {
        postSensorData(raw, "preheat");
      }

      flipAlreadyDone = false;
      flipTimerActive = false;
      flipWaitingAck = false;
      totalTimerActive = false;
      totalWaitingAck = false;
      forceImmediateTftDraw = true; // na start direct TFT updaten
    }
    else
    {
      double raw = mlx.getObjectTemperature();
      digitalWrite(relayPin, LOW);
      lcd.setRGB(0, 0, 255);
      lcd.clear();
      lcd.print("Gestopt");

      if (!isnan(raw))
      {
        postSensorData(raw, "stop");
      }
      currentSessionId = -1;

      tft.fillScreen(C_BG);
      drawMenuPage();

      flipTimerActive = false;
      flipWaitingAck = false;
      flipAlreadyDone = false;
      totalTimerActive = false;
      totalWaitingAck = false;
    }
  }

  // 5. PROCES
  if (isActive)
  {
    double raw = mlx.getObjectTemperature();
    if (isnan(raw))
    {
      Serial.println("MLX gaf NaN, skip cycle");
      return;
    }

    // virtuele temperatuur berekenen op basis van echte sensor
    double temp = getVirtualTemp(raw);

    // nu verwarmen: relais aan/uit op basis van targetTemp
    if (temp < targetTemp)
      digitalWrite(relayPin, HIGH);
    else
      digitalWrite(relayPin, LOW);

    unsigned long currentMillis = millis();

    // ====== STATUS + TIMERS ======
    const double tooHot = targetTemp + 10;
    const double tooCold = targetTemp - 10;
    const char *statusForDb;

    // LCD‑update
    if (currentMillis - previousLcdMillis >= lcdInterval)
    {
      previousLcdMillis = currentMillis;

      lcd.setCursor(0, 0);
      lcd.print("                ");

      if (temp > tooHot)
      {
        lcd.setRGB(255, 0, 0);
        lcd.setCursor(0, 0);
        lcd.print("!! TE HEET !!   ");
      }
      else if (temp < tooCold)
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

      lcd.setCursor(0, 1);
      lcd.print("                ");

      lcd.setCursor(0, 1);
      lcd.print("Nu:");
      lcd.print(temp, 1);
      lcd.print((char)223);
      lcd.print(" / ");
      lcd.print(targetTemp);
    }

    

    if (temp > tooHot)
    {
      statusForDb = "wait";
    }
    else if (temp < tooCold)
    {
      statusForDb = "preheat";
    }
    else
    {
      statusForDb = "cook";

      // flip-timer pas starten als stabiel
      if (!flipAlreadyDone && !flipTimerActive && currentFlipTime > 0)
      {
        flipTimerActive = true;
        flipStartMillis = millis();
        flipWaitingAck = false;
      }
    }

    // PERIODIEKE LOG NAAR SERVER (met echte sensorwaarde raw)
    if (currentMillis - lastPostMillis >= POST_INTERVAL_MS)
    {
      lastPostMillis = currentMillis;
      postSensorData(raw, statusForDb);
    }

    // ====== TFT TEKENEN MAX 1X PER SECONDE ======
    if ((currentMillis - previousTftMillis >= tftInterval) || forceImmediateTftDraw)
    {
      previousTftMillis = currentMillis;
      forceImmediateTftDraw = false;
      drawTftFrame(temp, statusForDb); // nu in één functie alles op TFT tekenen
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

// Temperatuur alleen tonen als geen timers actief zijn
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

  if (flipTimerActive || flipWaitingAck || totalTimerActive || totalWaitingAck)
  {
    return;
  }

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

// centrale functie: nu op scherm printen wat hoort bij de huidige state
void drawTftFrame(double currentTemp, const char *statusForDb)
{
  // eerst: temperatuurweergave als er geen timers actief zijn
  if (!flipTimerActive && !flipWaitingAck && !totalTimerActive && !totalWaitingAck)
  {
    updateTFTColor(currentTemp);
    return;
  }

  // FLIP-TIMER
  if (flipTimerActive && !flipAlreadyDone)
  {
    unsigned long elapsed = (millis() - flipStartMillis) / 1000;
    long remaining = currentFlipTime - (long)elapsed;

    if (remaining <= 0 && !flipWaitingAck)
    {
      // flip signaal
      flipWaitingAck = true;

      tft.fillRect(20, 65, 280, 80, C_BLACK);

      tft.setCursor(35, 80);
      tft.setTextColor(C_RED);
      tft.setTextSize(2);
      tft.print("Flip nu en druk");
      tft.setCursor(60, 105);
      tft.print("op de knop");

      tft.fillRect(0, 200, 320, 40, C_BG);
    }
    else if (remaining > 0)
    {
      // countdown naar flip
      tft.fillRect(20, 65, 280, 80, C_BLACK);

      tft.setCursor(40, 80);
      tft.setTextColor(C_WHITE);
      tft.setTextSize(2);
      tft.print("Flip over:");

      tft.setCursor(70, 105);
      tft.setTextColor(C_SELECT);
      tft.setTextSize(3);
      tft.print(remaining);
      tft.print("s/");
      tft.print(currentFlipTime);
      tft.print("s");
    }
    return;
  }

  // FLIP-MELDING (wacht op knop)
  if (flipWaitingAck)
  {
    // melding blijft staan, niks nieuws tekenen
    return;
  }

  // TOTALE TIJD (na flip)
  if (totalTimerActive && flipAlreadyDone && !totalWaitingAck)
  {
    unsigned long elapsedTotal = (millis() - totalStartMillis) / 1000;
    long remainingTotal = (currentTotalTime - currentFlipTime) - (long)elapsedTotal;

    if (remainingTotal <= 0)
    {
      totalWaitingAck = true;

      tft.fillRect(20, 65, 280, 80, C_BLACK);
      tft.setCursor(40, 80);
      tft.setTextColor(C_GREEN);
      tft.setTextSize(2);
      tft.print("Klaar, druk op");
      tft.setCursor(80, 105);
      tft.print("de knop");
    }
    else
    {
      tft.fillRect(20, 65, 280, 80, C_BLACK);

      tft.setCursor(40, 80);
      tft.setTextColor(C_WHITE);
      tft.setTextSize(2);
      tft.print("Nog bakken:");

      tft.setCursor(70, 105);
      tft.setTextColor(C_GREEN);
      tft.setTextSize(3);
      tft.print(remainingTotal);
      tft.print("s/");
      tft.print(currentTotalTime - currentFlipTime);
      tft.print("s");
    }
    return;
  }

  // TOTALE-TIJD-MELDING (wacht op knop)
  if (totalWaitingAck)
  {
    // melding blijft staan, niks nieuws tekenen
    return;
  }
}
