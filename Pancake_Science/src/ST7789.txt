#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

// --- DESIGN KLEUREN ---
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_GREY    0x5AEB
#define C_HEADER  0x001F // Donkerblauw
#define C_SELECT  0xFD20 // Oranje/Goud
#define C_BG      0x10A2 // Zeer donker grijs
#define C_GREEN   0x07E0
#define C_RED     0xF800

// --- INSTELLINGEN ---
const int ITEMS_PER_PAGE = 4; 

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
  {"Koffie", 92}
};

unsigned long previousLcdMillis = 0;
const long lcdInterval = 500;

int menuCount = sizeof(menus) / sizeof(menus[0]);
int menuIndex = 0; 
int lastEncState;           
bool isActive = false;  
int targetTemp = 0;   
uint16_t lastTFTColor = C_SELECT; // Hulpvariabele om kleur te onthouden

// --- FUNCTIES ---
void drawMenuPage();
void updateMenuItem(int index, bool selected);
void drawStaticInfoScreen(); 
void updateTFTColor(double currentTemp); // NIEUWE FUNCTIE

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
  tft.fillScreen(C_BG); 
  
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
          drawMenuPage(); 
      } else {
          updateMenuItem(oldIndex, false); 
          updateMenuItem(menuIndex, true); 
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
      lastTFTColor = C_SELECT; // Reset de kleur naar Oranje bij start
      
      // Teken het mooie info scherm
      drawStaticInfoScreen();

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
  // 3. PROCES (Tijdens verwarmen)
  // ==========================================
  if (isActive) {
    double temp = mlx.getObjectTemperature();

    // Relais sturing
    if (temp < targetTemp) digitalWrite(relayPin, HIGH);
    else digitalWrite(relayPin, LOW);

    updateTFTColor(temp);

    unsigned long currentMillis = millis();
    
    if (currentMillis - previousLcdMillis >= lcdInterval) {
      previousLcdMillis = currentMillis;

      if (temp > (targetTemp + 5)) {
        lcd.setRGB(255, 0, 0); 
        lcd.setCursor(0, 0);
        lcd.print("!! TE HEET !!   "); // Spaties vullen oude tekst
      } else if (temp < (targetTemp - 2)) {
        lcd.setRGB(255, 100, 0); 
        lcd.setCursor(0, 0);
        lcd.print("Opwarmen...     ");
      } else {
        lcd.setRGB(0, 255, 0); 
        lcd.setCursor(0, 0);
        lcd.print("Stabiel         ");
      }
      
      // Temp op LCD (Regel 2)
      lcd.setCursor(0, 1);
      lcd.print("Nu:");
      lcd.print(temp, 1);
      lcd.print((char)223); 
      lcd.print(" / ");
      lcd.print(targetTemp);
      // Eventueel extra spaties printen om restanten te wissen
      lcd.print(" "); 
    }
  }
}

// ==========================================
// GRAFISCHE FUNCTIES
// ==========================================

void drawMenuPage() {
  tft.fillRect(0, 0, 320, 40, C_HEADER);
  tft.drawFastHLine(0, 40, 320, C_WHITE); 
  tft.setCursor(60, 10);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(3);
  tft.print("CHEF MENU");
  tft.fillRect(0, 41, 320, 130, C_BG);

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
  int yPos = 55 + (relativePos * 30); 

  if (selected) {
    tft.fillRoundRect(10, yPos, 300, 26, 5, C_SELECT);
    tft.setTextColor(C_BLACK); 
  } else {
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

void drawStaticInfoScreen() {
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

  // We tekenen hem initieel in Oranje/Goud
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

// --- NIEUWE FUNCTIE: UPDATE ALLEEN DE KLEUR ---
void updateTFTColor(double currentTemp) {
  uint16_t newColor;

  // 1. Bepaal de nieuwe kleur op basis van temperatuur
  if (currentTemp > (targetTemp + 5)) {
    newColor = C_RED;
  } else if (currentTemp < (targetTemp - 2)) {
    newColor = C_SELECT; // Oranje
  } else {
    newColor = C_GREEN;
  }

  // 2. Als de kleur anders is dan de vorige keer -> Teken opnieuw!
  if (newColor != lastTFTColor) {
    
    // Wis het oude getal (Zwart blokje eroverheen)
    // Coordinaten moeten overeenkomen met drawStaticInfoScreen
    tft.fillRect(115, 95, 120, 40, C_BLACK); 

    // Teken het getal in de NIEUWE kleur
    tft.setCursor(120, 100);
    tft.setTextColor(newColor);
    tft.setTextSize(4);
    tft.print(targetTemp);
    tft.print("C");

    // Onthoud de nieuwe kleur zodat we niet blijven flikkeren
    lastTFTColor = newColor;
  }
}