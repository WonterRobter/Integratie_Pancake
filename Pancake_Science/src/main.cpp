#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MLX90632.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

// ================== HARDWARE ==================
Adafruit_MLX90632 mlx;

const int relayPin = 7;
const int buttonPin = 6;

const int encCLK = 2;
const int encDT = 3;
const int encSW = 4;

const int ledR = 9; // RGB common anode
const int ledG = 10;
const int ledB = 11;



WiFiClient espClient;
PubSubClient client(espClient);

// ================== VARS ==================
bool sessionActive = false;
bool heatingOn = false;
float setpoint = 60.0;
float hysteresis = 2.0;

int lastCLKState;

// ================== RGB FUNCTION ================== na checken (Common anode)
void setRGB(int r, int g, int b)
{
  analogWrite(ledR, 255 - r);
  analogWrite(ledG, 255 - g);
  analogWrite(ledB, 255 - b);
}

// ================== MQTT CALLBACK ==================
void callback(char *topic, byte *payload, unsigned int length)
{
  String msg;
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  if (String(topic) == "pancake/command/start")
  {
    sessionActive = (msg == "1");
    digitalWrite(relayPin, sessionActive ? HIGH : LOW);
  }
}

// ================== CONNECT WIFI ==================
void setup_wifi()
{
  delay(100);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
}

// ================== CONNECT MQTT ==================
void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Connecting MQTT...");
    if (client.connect("pancakeController"))
    {
      Serial.println("connected");
      client.subscribe("pancake/command/#");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

// ================== SETUP ==================
void setup()
{
  Serial.begin(115200);
  Wire.begin();

  // Sensor
  if (!mlx.begin())
  {
    while (1)
    {
      delay(10);
    }
  }
  mlx.reset();
  mlx.setMode(MLX90632_MODE_CONTINUOUS);
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);
  mlx.resetNewData();

  // Pins
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(encCLK, INPUT);
  pinMode(encDT, INPUT);
  pinMode(encSW, INPUT_PULLUP);
  pinMode(ledR, OUTPUT);
  pinMode(ledG, OUTPUT);
  pinMode(ledB, OUTPUT);
  setRGB(255, 255, 0); // blauw idle

  lastCLKState = digitalRead(encCLK);

  // WiFi + MQTT
  setup_wifi();
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);

  Serial.println("Setup klaar");
}

// ================== LOOP ==================
void loop()
{
  if (!client.connected())
    reconnect();
  client.loop();

  // --- Start/Stop knop ---
  static bool lastButton = HIGH;
  bool btn = digitalRead(buttonPin);
  if (btn == LOW && lastButton == HIGH)
  {
    delay(50);
    sessionActive = !sessionActive;
    digitalWrite(relayPin, sessionActive ? HIGH : LOW);
    Serial.println(sessionActive ? "Session gestart" : "Session gestopt");
  }
  lastButton = btn;

  // --- Rotary encoder ---
  int clkState = digitalRead(encCLK);
  if (clkState != lastCLKState)
  {
    if (digitalRead(encDT) != clkState)
      setpoint += 0.5;
    else
      setpoint -= 0.5;
    setpoint = constrain(setpoint, 30, 120);
    Serial.print("Nieuw setpoint: ");
    Serial.println(setpoint);
    lastCLKState = clkState;
  }

  // --- Meting & bang-bang regeling ---
  if (sessionActive && mlx.isNewData())
  {
    float objTemp = mlx.getObjectTemperature();
    float ambTemp = mlx.getAmbientTemperature();

    heatingOn = (objTemp < setpoint - hysteresis) ? true : (objTemp > setpoint + hysteresis ? false : heatingOn);
    digitalWrite(relayPin, heatingOn ? HIGH : LOW);

    // RGB
    if (!sessionActive)
      setRGB(0, 0, 255);
    else if (heatingOn)
      setRGB(255, 140, 0);
    else
      setRGB(0, 255, 0);

    // MQTT / WebApp
    String payload = String(objTemp, 2) + "," + String(ambTemp, 2) + "," + String(setpoint, 1) + "," + String(heatingOn ? 1 : 0);
    client.publish("pancake/telemetry", payload.c_str());

    mlx.resetNewData();
  }

  delay(200);
}
