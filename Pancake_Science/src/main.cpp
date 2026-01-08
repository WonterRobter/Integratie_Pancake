#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MLX90632.h"
#include "DFRobot_RGBLCD1602.h"

// --- INSTELLINGEN ---
Adafruit_MLX90632 mlx = Adafruit_MLX90632();
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2); // Scherm op adres 0x2D

const int relayPin = 7;  // Warmtemat
const int buttonPin = 6; // Knopje
bool isAan = false;      // Houdt bij of we aan of uit staan

void setup() {
  Serial.begin(115200);

  // 1. Initialiseer LCD
  lcd.init();
  lcd.setRGB(0, 0, 255); // Start met Blauw (Standby)
  lcd.print("Systeem Start...");
  delay(1000);

  // 2. Initialiseer Sensor
  if (!mlx.begin()) {
    Serial.println("Geen sensor gevonden!");
    lcd.setRGB(255, 0, 0); // Rood bij fout
    lcd.setCursor(0, 0);
    lcd.print("Sensor Fout!");
    while (1);
  }

  mlx.setMode(MLX90632_MODE_CONTINUOUS); // Continue modus

  // 3. Pinnen
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  // Scherm klaarzetten voor start
  lcd.clear();
  lcd.setRGB(0, 0, 255); // Blauw
  lcd.print("Druk op Start");
}

void loop() {
  // ==========================================
  // 1. KNOP CONTROLEREN
  // ==========================================
  if (digitalRead(buttonPin) == LOW) {
    isAan = !isAan; // Wissel status
    
    if (isAan) {
      digitalWrite(relayPin, HIGH); // Relais AAN
      Serial.println("--- GESTART ---");
      lcd.clear(); // Scherm schoonmaken voor nieuwe tekst
    } else {
      digitalWrite(relayPin, LOW);  // Relais UIT
      Serial.println("--- GESTOPT ---");
      
      // Zet scherm op "Pauze" stand (Blauw)
      lcd.setRGB(0, 0, 255); 
      lcd.clear();
      lcd.print("Sessie Gestopt");
    }
    delay(500); // Anti-dubbelklik
  }

  // ==========================================
  // 2. METEN & SCHERM UPDATEN (Alleen als AAN)
  // ==========================================
  if (isAan) {
    double matTemp = mlx.getObjectTemperature();
    // double omgevingTemp = mlx.getAmbientTemperature(); // Optioneel als je ruimte hebt

    // --- KLEUR BEPALEN ---
    if (matTemp >= 30.0) {
      lcd.setRGB(255, 0, 0); // ROOD (Boven 30 graden)
    } else {
      lcd.setRGB(0, 255, 0); // GROEN (Onder 30 graden)
    }

    // --- TEKST OP SCHERM ---
    // Regel 1: Status
    lcd.setCursor(0, 0);
    if (matTemp >= 30.0) {
      lcd.print("LET OP: WARM!   "); 
    } else {
      lcd.print("Verwarmen...    ");
    }

    // Regel 2: Temperatuur
    lcd.setCursor(0, 1);
    lcd.print("Mat: ");
    lcd.print(matTemp, 1); // 1 decimaal (bijv 28.5)
    lcd.print((char)223);  // Graden teken (°)
    lcd.print("C    ");      // Extra spaties om oude tekst te wissen

    // Log ook naar serial monitor
    Serial.print("Mat: "); Serial.println(matTemp);

    delay(200); // Rustige update snelheid
  }
}