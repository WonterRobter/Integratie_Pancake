#include <Arduino.h>
#include "Adafruit_MLX90632.h"

Adafruit_MLX90632 mlx = Adafruit_MLX90632();

// ---------- Hardware ----------
const int relayPin = 7;  // Relay aansturen warmtemat
const int buttonPin = 6; // Start/Stop knop

bool sessionActive = false; // sessie status

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    delay(10);

  // Sensor initialisatie
  Serial.println(F("Adafruit MLX90632 test"));
  if (!mlx.begin())
  {
    Serial.println(F("Failed to find MLX90632 chip"));
    while (1)
      delay(10);
  }
  Serial.println(F("MLX90632 Found!"));

  mlx.reset();
  mlx.setMode(MLX90632_MODE_CONTINUOUS);
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);
  mlx.resetNewData();

  // ---------- Pins ----------
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Relay uit bij start

  pinMode(buttonPin, INPUT_PULLUP); // knop met interne pullup
}

void loop()
{
  // ---------- Knop detectie ----------
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(buttonPin);

  if (buttonState == LOW && lastButtonState == HIGH)
  {            // togglet bij druk
    delay(50); // debounce
    sessionActive = !sessionActive;
    if (sessionActive)
    {
      Serial.println("=== Session gestart ===");
      digitalWrite(relayPin, HIGH); // warmtemat aan
    }
    else
    {
      Serial.println("=== Session gestopt ===");
      digitalWrite(relayPin, LOW); // warmtemat uit
    }
  }
  lastButtonState = buttonState;

  // ---------- Temperatuur uitlezen ----------
  if (mlx.isNewData() && sessionActive)
  { // alleen meten tijdens sessie
    double ambientTemp = mlx.getAmbientTemperature();
    double objectTemp = mlx.getObjectTemperature();

    Serial.print("Ambient Temp: ");
    Serial.print(ambientTemp, 2);
    Serial.print(" °C, Object Temp: ");
    Serial.print(objectTemp, 2);
    Serial.println(" °C");

    mlx.resetNewData();
  }

  // Voor step modes, trigger measurement
  mlx90632_mode_t currentMode = mlx.getMode();
  if (currentMode == MLX90632_MODE_STEP || currentMode == MLX90632_MODE_SLEEPING_STEP)
  {
    mlx.startSingleMeasurement();
  }

  delay(500);
}