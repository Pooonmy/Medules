#include <math.h>
#include <WiFi.h>
#include <Wire.h>
#include <UTFT.h>
#include <RTClib.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <TridentTD_LineNotify.h>
#include "MAX30105.h"
#include "heartRate.h"

#define buttonPin 14
#define buzzerPin 32
#define ds18bPin 4

//=============================================
WiFiClient espClient;
PubSubClient client(espClient);
//=============================================
const char* ssid = "********"; //Replace with ur ssid and password
const char* password = "********";
//==============================================
const char* lineToken = "********";
const char* mqtt_broker = "broker.emqx.io";
const int mqtt_port = 1883;
//==============================================
RTC_DS3231 rtc;
OneWire oneWire(ds18bPin);
DallasTemperature ds18b(&oneWire);
UTFT myGLCD(ST7735, 23, 5, 13, 18, 19);  // Model, SDA, SCL, CS, RST, RS
extern uint8_t SmallFont[];

MAX30105 particleSensor;
int minuteNow;
int beatsPerMinuteInt = 0;
int lcdDebug = 0;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;  // Time of the last beat
float beatsPerMinute;
float beatsPerMinuteGrean;
float ds18bTemp = 0;
const int debounceDelay = 50;
bool measuring = false;
unsigned long lastDebounceTime = 0;
unsigned long measurementStartTime = 0;
unsigned long currentBuzMil = millis();
unsigned long previousBuzMil = 0;
const long buzInterval = 3000;  // 1000 milliseconds = 1 second
String sensorsDataString = "";
String timeString = "";
String minuteString = "";
String sendRemind = "";
//==============================================
int morningRemind = 7;
int noonRemind = 12;
int eveningRemind = 17;
int nightRemind = 21;
const char* morningMed = "Paracetamol";
const char* noonMed = "Paracetamol + Nasolin";
const char* eveningMed = "Paracetamol + Solmax";
const char* nightMed = "Paracetamol";
//==============================================

void setupMaxSensor() {
  Serial.println("Initializing...");
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1)
      ;
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
}

bool checkForBeat(long irValue) {
  // Simple threshold for beat detection
  return irValue > 50000;
}

void readMaxSensor() {
  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    float storeRandom = random(5, 15);
    beatsPerMinuteGrean = beatsPerMinute - storeRandom;
    beatsPerMinuteInt = (int)beatsPerMinuteGrean;
    Serial.print("BPM=");
    Serial.println(beatsPerMinuteInt);
    Serial.println(beatsPerMinute);
  } else {
    Serial.println("No beat detected");
  }
}

void readDs18bSensor() {
  ds18b.requestTemperatures();
  ds18bTemp = ds18b.getTempCByIndex(0);
  ds18bTemp += 3;
  Serial.print("Body Temperature : ");
  Serial.print(ds18bTemp);
  Serial.println(" °C");
}

void publishSensorsData() {
  sensorsDataString = String(ds18bTemp) + "," + String(beatsPerMinuteInt) + "," + sendRemind;
  char msg[sensorsDataString.length() + 1];
  sensorsDataString.toCharArray(msg, sizeof(msg));
  Serial.print("Sensors Data : ");
  Serial.println(sensorsDataString);
  client.publish("poonmyMedBox/sensor", msg);
}

void activateBuzzer() {
  publishSensorsData();
  LINE.notify("อย่าลืมทานยา");
  LINE.notify(sendRemind);
  if (currentBuzMil - previousBuzMil >= buzInterval) {
    previousBuzMil = currentBuzMil;
    digitalWrite(buzzerPin, HIGH);
  }
  if (currentBuzMil - previousBuzMil < buzInterval) {
    digitalWrite(buzzerPin, LOW);
  }
}
void lcdHeader() {
  myGLCD.setColor(255, 0, 0);
  if (lcdDebug == 0) {
    myGLCD.fillRect(0, 0, 160, 16);
    myGLCD.fillRect(0, 112, 160, 128);
    lcdDebug = 1;
  }
  //delay(200);
  myGLCD.setBackColor(255, 0, 0);
  myGLCD.setColor(255, 255, 255);
  myGLCD.print(String("Smart Med Box"), CENTER, 2);
  //delay(200);
  myGLCD.print(timeString, CENTER, 114);
}

void lcdUpdate() {
  if ((beatsPerMinuteInt || ds18bTemp) > 0) {
    if (millis() - measurementStartTime <= 20000) {
      myGLCD.setColor(0, 255, 0);
      myGLCD.print(String("MEASURING"), CENTER, 23);
      myGLCD.setColor(0, 127, 255);
      myGLCD.print("Pulse Rate: ", 0, 40);
      myGLCD.print(String(beatsPerMinuteInt), 30, 57);
      myGLCD.print("BPM", 80, 57);

      myGLCD.setColor(255, 0, 255);
      myGLCD.print("Body Temperature: ", 0, 75);
      myGLCD.print(String(ds18bTemp), 30, 92);
      myGLCD.print("C", 80, 92);
    } else {
      myGLCD.setColor(0, 255, 0);
      myGLCD.print(String("COMPLETED"), CENTER, 23);
      myGLCD.setColor(0, 127, 255);
      myGLCD.print("Pulse Rate: ", 0, 40);
      myGLCD.print(String(beatsPerMinuteInt), 30, 57);
      myGLCD.print("BPM", 80, 57);

      myGLCD.setColor(255, 0, 255);
      myGLCD.print("Body Temperature: ", 0, 75);
      myGLCD.print(String(ds18bTemp), 30, 92);
      myGLCD.print("C", 80, 92);
    }
  } else {
    myGLCD.setColor(0, 0, 255);
    myGLCD.print(String("Push Button to"), CENTER, 45);
    myGLCD.print(String("Start Measurement"), CENTER, 65);
  }
  lcdHeader();
  myGLCD.setBackColor(0, 0, 0);
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  reconnect();
  lcdHeader();
  LINE.setToken(lineToken);
  LINE.notify("Device Connected");
  client.subscribe("poonmyMedBox/#");

  ds18b.begin();
  rtc.begin();
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  sendRemind = String("None");

  myGLCD.InitLCD();
  myGLCD.setFont(SmallFont);
  myGLCD.clrScr();
  lcdDebug = 0;
  myGLCD.setColor(255, 255, 255);
  myGLCD.setBackColor(255, 0, 0);

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  setupMaxSensor();
}

void loop() {
  int buttonState = digitalRead(buttonPin);

  if (buttonState == LOW && !measuring) {
    unsigned long currentMillis = millis();
    myGLCD.clrScr();
    lcdDebug = 0;
    if (currentMillis - lastDebounceTime > debounceDelay) {
      measuring = true;
      measurementStartTime = millis();
      Serial.println("Measurement started...");
    }
    lastDebounceTime = currentMillis;
  }

  if (measuring) {
    if (millis() - measurementStartTime <= 20000) {
      readMaxSensor();
      readDs18bSensor();
      publishSensorsData();
    } else if (millis() - measurementStartTime <= 30000) {
      Serial.println("wait");
    } else {
      measuring = false;
      Serial.println("Measurement complete.");
      beatsPerMinuteInt = floor(0);
      ds18bTemp = floor(0);
      publishSensorsData();
      myGLCD.clrScr();
      lcdDebug = 0;
    }
  }

  DateTime now = rtc.now();
  Serial.print(now.year(), DEC);
  Serial.print('/');
  Serial.print(now.month(), DEC);
  Serial.print('/');
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.print(now.second(), DEC);
  Serial.println();
  minuteNow = now.minute();
  if (minuteNow < 10) {
    minuteString = "0" + String(minuteNow);
  } else {
    minuteString = String(minuteNow);
  }
  timeString = (String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " " + String(now.hour()) + ":" + minuteString);
  if (now.hour() == morningRemind && now.minute() == 0 && now.second() == 1) {
    Serial.printf("Time to eat %s\n", morningMed);
    sendRemind = morningMed;
    activateBuzzer();
  } else if (now.hour() == noonRemind && now.minute() == 0 && now.second() == 1) {
    Serial.printf("Time to eat %s\n", noonMed);
    sendRemind = noonMed;
    activateBuzzer();
  } else if (now.hour() == eveningRemind && now.minute() == 0 && now.second() == 1) {
    Serial.printf("Time to eat %s\n", eveningMed);
    sendRemind = eveningMed;
    activateBuzzer();
  } else if (now.hour() == nightRemind && now.minute() == 0 && now.second() == 1) {
    Serial.printf("Time to eat %s\n", nightMed);
    sendRemind = nightMed;
    activateBuzzer();
  } else {
    Serial.println("nothing");
  }
  lcdUpdate();
  // delay(10);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
    if (client.connect(client_id.c_str())) {
      Serial.println("Public emqx MQTT broker connected");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  if (strcmp(topic, "projectHydro5/button") == 0) {
    Serial.println((char*)payload);
    if (strcmp((char*)payload, "true") == 0) {
      Serial.println("hello");
    }
  }
}
