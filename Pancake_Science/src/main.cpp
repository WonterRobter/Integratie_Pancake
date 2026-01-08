#include <Arduino.h>
#include "Adafruit_MLX90632.h"

Adafruit_MLX90632 mlx = Adafruit_MLX90632();

const int relayPin = 7;  // Warmtemat
const int buttonPin = 6; // Knopje
bool isAan = false;      // Houdt bij of we aan of uit staan

void setup() {
  Serial.begin(115200);
  
  // Start de sensor
  if (!mlx.begin()) {
    Serial.println("Geen sensor gevonden!");
    while (1);
  }

  // Zet sensor in continue modus (nodig voor stabiele lezing)
  mlx.setMode(MLX90632_MODE_CONTINUOUS); 

  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
}

void loop() {
  // --- 1. KNOP CONTROLEREN ---
  if (digitalRead(buttonPin) == LOW) {
    // Knop is ingedrukt!
    isAan = !isAan; // Wissel status
    
    if (isAan) {
      digitalWrite(relayPin, HIGH); // Zet mat AAN
      Serial.println("--- GESTART ---");
    } else {
      digitalWrite(relayPin, LOW);  // Zet mat UIT
      Serial.println("--- GESTOPT ---");
    }

    delay(500); // Wacht 0.5 sec tegen dubbel klikken
  }

  // --- 2. TEMPERATUUR METEN ---
  if (isAan) {
    double omgeving = mlx.getAmbientTemperature(); // Kamer
    double object = mlx.getObjectTemperature();    // Mat

    Serial.print("Omgeving: ");
    Serial.print(omgeving);
    Serial.print(" °C  |  Mat: ");
    Serial.print(object);
    Serial.println(" °C");
    
    delay(200); // Kleine pauze zodat de tekst leesbaar blijft
  }
}