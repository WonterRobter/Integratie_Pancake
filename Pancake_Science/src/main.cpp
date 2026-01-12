#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

// --- DESIGN KLEUREN (Modern) ---
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GREY    0x5AEB
#define C_HEADER  0x001F // Donkerblauw
#define C_SELECT  0xFD20 // Oranje/Goud
#define C_BG      0x10A2 // Zeer donker grijs
#define C_GREEN   0x07E0
#define C_RED     0xF800

// --- INSTELLINGEN ---
const int ITEMS_PER_PAGE = 4; // Iets minder items, maar groter en mooier

// --- PIN LAYOUT ---
const int encPinA = 3;   
const int encPinB = 5;   
const int encBtn  = 6;
#define TFT_CS    10
#define TFT_RST   9
#define TFT_DC    8
const int relayPin = 7; 

// --- OBJECTEN ---
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_MLX90632 mlx = Adafruit_MLX90632();
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2); 

// --- MENU DATA ---
struct Programma {
  String naam;
  int temp;
};

Programma menus[] = {
  {"Handwarm", 30},
  {"Slow Cook", 60},
  {"Thee Water", 80},
  {"Koken", 100},
  {"Braden", 140},
  {"Pasta", 100},
  {"Soep", 90},
  {"Chocolade", 45},
  {"Warmhouden", 50},
  {"Boost Mode", 120},
  {"Sous Vide", 56},
  {"Koffie", 92}
};

int menuCount = sizeof(menus) / sizeof(menus[0]);
int menuIndex = 0; 
int lastEncState;           
bool isActive = false;  
int targetTemp = 0;   

// --- FUNCTIES ---
void drawMenuPage();
void updateMenuItem(int index, bool selected);
void drawCookingScreen(double currentTemp);

void setup() {
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
  if (!mlx.begin()) {
    lcd.setRGB(255, 0, 0);
    lcd.print("Sensor Fout");
    while (1); 
  }
  mlx.setMode(MLX90632_MODE_CONTINUOUS);

  // TFT Init
  tft.init(170, 320); 
  tft.setRotation(3); 
  tft.fillScreen(C_BG); // Achtergrondkleur
  
  drawMenuPage();
}

void loop() {
  // ==========================================
  // 1. NAVIGATIE
  // ==========================================
  if (!isActive) {
    int currentEncState = digitalRead(encPinA);

    if (currentEncState != lastEncState && currentEncState == LOW) {
      int oldIndex = menuIndex;
      int oldPage = oldIndex / ITEMS_PER_PAGE;

      if (digitalRead(encPinB) != currentEncState) menuIndex++; 
      else menuIndex--; 

      if (menuIndex >= menuCount) menuIndex = 0;        
      if (menuIndex < 0) menuIndex = menuCount - 1;     

      int newPage = menuIndex / ITEMS_PER_PAGE;

      if (newPage != oldPage) {
          drawMenuPage(); // Nieuwe pagina opbouwen
      } else {
          updateMenuItem(oldIndex, false); // Oude deselecteren
          updateMenuItem(menuIndex, true); // Nieuwe selecteren
      }
    }
    lastEncState = currentEncState;
  }

  // ==========================================
  // 2. KNOP START/STOP
  // ==========================================
  if (digitalRead(encBtn) == LOW) {
    delay(50); 
    while(digitalRead(encBtn) == LOW); 
    delay(50);

    isActive = !isActive; 

    if (isActive) {
      // --- START ---
      targetTemp = menus[menuIndex].temp;
      
      // Teken het "Kook Scherm"
      tft.fillScreen(C_BLACK);
      
      // Header
      tft.fillRect(0, 0, 320, 40, C_HEADER);
      tft.setCursor(10, 10);
      tft.setTextColor(C_WHITE);
      tft.setTextSize(3);
      tft.print(menus[menuIndex].naam);

      // Doel temp klein
      tft.setCursor(10, 140);
      tft.setTextSize(2);
      tft.setTextColor(C_GREY);
      tft.print("Doel: ");
      tft.print(targetTemp);
      tft.print(" C");

    } else {
      // --- STOP ---
      digitalWrite(relayPin, LOW);
      lcd.setRGB(0, 0, 255);
      lcd.clear();
      lcd.print("Gestopt");
      
      // Terug naar menu
      tft.fillScreen(C_BG);
      drawMenuPage();
    }
  }

  // ==========================================
  // 3. PROCES & VISUALISATIE
  // ==========================================
  if (isActive) {
    double temp = mlx.getObjectTemperature();

    // Relais sturing
    if (temp < targetTemp) digitalWrite(relayPin, HIGH);
    else digitalWrite(relayPin, LOW);

    // Update TFT Kookscherm (Alleen de waarde!)
    drawCookingScreen(temp);

    // Update LCD (Backup display)
    if (temp > (targetTemp + 5)) {
      lcd.setRGB(255, 0, 0); 
      lcd.setCursor(0, 0);
      lcd.print("!! TE HEET !!   ");
    } else if (temp < (targetTemp - 2)) {
      lcd.setRGB(255, 100, 0); 
      lcd.setCursor(0, 0);
      lcd.print("Opwarmen...     ");
    } else {
      lcd.setRGB(0, 255, 0); 
      lcd.setCursor(0, 0);
      lcd.print("Stabiel         ");
    }
    
    // Temp op LCD
    lcd.setCursor(0, 1);
    lcd.print(temp, 1);
    lcd.print((char)223); 

    delay(200); 
  }
}

// ==========================================
// GRAFISCHE FUNCTIES
// ==========================================

void drawMenuPage() {
  // 1. Teken Header Balk
  tft.fillRect(0, 0, 320, 40, C_HEADER);
  tft.drawFastHLine(0, 40, 320, C_WHITE); // Lijntje eronder
  
  tft.setCursor(60, 10);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.print("CHEF MENU");

  // 2. Wis het lijst gedeelte (niet de header)
  tft.fillRect(0, 41, 320, 130, C_BG);

  // 3. Teken de items
  int currentPage = menuIndex / ITEMS_PER_PAGE;
  int startIdx = currentPage * ITEMS_PER_PAGE;

  for (int i = 0; i < ITEMS_PER_PAGE; i++) {
    int realIndex = startIdx + i;
    if (realIndex < menuCount) {
        updateMenuItem(realIndex, (realIndex == menuIndex));
    }
  }
}

void updateMenuItem(int index, bool selected) {
  int currentPage = menuIndex / ITEMS_PER_PAGE;
  int itemPage = index / ITEMS_PER_PAGE;
  if (currentPage != itemPage) return; 

  int relativePos = index % ITEMS_PER_PAGE;
  int yPos = 55 + (relativePos * 30); // 30 pixels per regel

  // Teken achtergrond van de knop
  if (selected) {
    // Geselecteerd: Oranje ronde rechthoek
    tft.fillRoundRect(10, yPos, 300, 26, 5, C_SELECT);
    tft.setTextColor(C_BLACK); // Zwarte tekst op oranje
  } else {
    // Niet geselecteerd: Donkere achtergrond
    tft.fillRoundRect(10, yPos, 300, 26, 5, C_BG); 
    tft.setTextColor(C_WHITE); // Witte tekst op donker
  }

  // Teken tekst
  tft.setCursor(20, yPos + 4);
  tft.setTextSize(2);
  tft.print(menus[index].naam);

  // Teken temperatuur rechts
  tft.setCursor(240, yPos + 4);
  tft.print(menus[index].temp);
  tft.print("C");
}

void drawCookingScreen(double currentTemp) {
  // We updaten alleen het middelste gedeelte om knipperen te voorkomen
  // Wis oude cijfers (zwarte rechthoek erover)
  tft.fillRect(40, 60, 240, 60, C_BLACK);
  
  // Kies kleur op basis van status
  if (currentTemp < targetTemp - 2) tft.setTextColor(C_SELECT); // Oranje (koud)
  else if (currentTemp > targetTemp + 5) tft.setTextColor(C_RED); // Rood (heet)
  else tft.setTextColor(C_GREEN); // Groen (goed)

  // Grote tekst in het midden
  tft.setTextSize(6); // Heel groot!
  tft.setCursor(50, 70);
  tft.print(currentTemp, 1);
}