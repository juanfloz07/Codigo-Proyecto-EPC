#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// HC-05
SoftwareSerial BT(7, 6);

// AD8232
const int ECG_PIN  = A0;
const int LO_PLUS  = 10;
const int LO_MINUS = 11;


// MAX30105
MAX30105 particleSensor;

// Buffers - MAX
const byte SPO2_BUFFER_SIZE = 100;
uint32_t irBuffer[SPO2_BUFFER_SIZE];
uint32_t redBuffer[SPO2_BUFFER_SIZE];

// Resultados SpO2
int32_t spo2 = 0;
int8_t validSpO2 = 0;
int32_t heartRateAlgo = 0;
int8_t validHeartRate = 0;

// Presencia de dedo
const long FINGER_THRESHOLD = 5000;

// Lectura por lotes 
const byte READ_BATCH = 25;

// Variables de estado
long irValue = 0;
long redValue = 0;
bool fingerDetected = false;

// BPM promedio simple por beats
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {0};
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;


// MPU6050
Adafruit_MPU6050 mpu;

// Respiracion
const unsigned long RESP_SAMPLE_INTERVAL_MS = 20;  // 50 Hz
unsigned long lastRespSample = 0;

float baseline = 0.0;
float respSignal = 0.0;
float ampTrack = 0.0;

const float ALPHA_BASE = 0.01;
const float ALPHA_RESP = 0.10;
const float ALPHA_AMP  = 0.03;

bool aboveThreshold = false;
float peakValue = 0.0;
unsigned long peakTime = 0;
unsigned long lastBreathTime = 0;

const unsigned long MIN_BREATH_INTERVAL_MS = 1600;  // ~50 RPM max

const int RESP_BUF_SIZE = 6;
unsigned long breathIntervals[RESP_BUF_SIZE];
int respBufIndex = 0;
int validIntervals = 0;
float respRPM = 0.0;

float lastMag = 0.0;
float lastThreshold = 0.0;

int temperature = 0;


// Tiempos generales
unsigned long lastECGRead = 0;
unsigned long lastSend = 0;
unsigned long lastOLED = 0;

const unsigned long ecgInterval  = 20;   // 50 Hz
const unsigned long sendInterval = 20;   // 50 Hz hacia Bluetooth
const unsigned long oledInterval = 300;  // OLED cada 300 ms


// ECG filtrado
int ecgValue = 0;
int ecgValueGraph = -1;
bool ecgConnected = false;

const int ECG_FILTER_SIZE = 10;
int ecgBuffer[ECG_FILTER_SIZE] = {0};
int ecgBufferIndex = 0;
long ecgSum = 0;


// Utilidades
float ema(float prev, float input, float alpha) {
  return prev + alpha * (input - prev);
}

float absf(float x) {
  return (x >= 0) ? x : -x;
}

float averageInterval() {
  if (validIntervals == 0) return 0.0;

  unsigned long sum = 0;
  for (int i = 0; i < validIntervals; i++) {
    sum += breathIntervals[i];
  }
  return (float)sum / validIntervals;
}

void registerBreath(unsigned long t) {
  if (lastBreathTime != 0) {
    unsigned long dt = t - lastBreathTime;

    breathIntervals[respBufIndex] = dt;
    respBufIndex = (respBufIndex + 1) % RESP_BUF_SIZE;

    if (validIntervals < RESP_BUF_SIZE) {
      validIntervals++;
    }

    float avgDt = averageInterval();
    if (avgDt > 0) {
      respRPM = 60000.0 / avgDt;
    }
  }

  lastBreathTime = t;
}

void resetECGFilter() {
  ecgSum = 0;
  ecgBufferIndex = 0;
  for (int i = 0; i < ECG_FILTER_SIZE; i++) {
    ecgBuffer[i] = 0;
  }
}

void resetRespiration() {
  respRPM = 0.0;
  validIntervals = 0;
  respBufIndex = 0;
  lastBreathTime = 0;
  aboveThreshold = false;
}
void fillInitialSpO2Buffer() {
  for (byte i = 0; i < SPO2_BUFFER_SIZE; i++) {
    while (particleSensor.available() == false) {
      particleSensor.check();
    }

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }
}

void computeSpO2() {
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer,
    SPO2_BUFFER_SIZE,
    redBuffer,
    &spo2,
    &validSpO2,
    &heartRateAlgo,
    &validHeartRate
  );
}

void shiftSpO2BuffersLeft(byte n) {
  for (byte i = n; i < SPO2_BUFFER_SIZE; i++) {
    redBuffer[i - n] = redBuffer[i];
    irBuffer[i - n] = irBuffer[i];
  }
}

void readNewSpO2Samples(byte n) {
  for (byte i = SPO2_BUFFER_SIZE - n; i < SPO2_BUFFER_SIZE; i++) {
    while (particleSensor.available() == false) {
      particleSensor.check();
    }

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }
}

void resetMaxState() {
  spo2 = 0;
  validSpO2 = 0;
  heartRateAlgo = 0;
  validHeartRate = 0;
  beatAvg = 0;
  beatsPerMinute = 0;
}




void setup() {
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  Serial.begin(115200);
  BT.begin(9600);
  Wire.begin();

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("No se encontro OLED");
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Iniciando sistema...");
  display.display();

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("No se encontro MAX30102");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Error MAX30102");
    display.display();
    while (true);
  }

  //  SpO2
  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;       // Red + IR
  int sampleRate = 100;  
  int pulseWidth = 411;
  int adcRange = 4096;

  particleSensor.setup(
    ledBrightness,
    sampleAverage,
    ledMode,
    sampleRate,
    pulseWidth,
    adcRange
  );

  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Preparando MAX...");
  display.display();

  fillInitialSpO2Buffer();
  computeSpO2();

  // MPU6050
  if (!mpu.begin()) {
    Serial.println("No se encontro MPU6050");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Error MPU6050");
    display.display();
    while (true);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // Calibracion inicial respiracion
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Calibrando resp...");
  display.println("Quieto 3 s");
  display.display();

  Serial.println("3 segundos para calibrar MPU6050");
  long n = 0;
  float sum = 0.0;
  unsigned long t0 = millis();

  while (millis() - t0 < 3000) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    temperature = temp.temperature;

    float mag = sqrt(
      a.acceleration.x * a.acceleration.x +
      a.acceleration.y * a.acceleration.y +
      a.acceleration.z * a.acceleration.z
    );
    
    sum += mag;
    n++;
    delay(10);
  }

  baseline = (n > 0) ? (sum / n) : 0.0;
  respSignal = 0.0;
  ampTrack = 0.0;
  respRPM = 0.0;

  Serial.println("Sistema iniciado");

  Serial.println("ECG,IR,RED,BPM,SPO2,RESPSIG,RPM,FINGER");

  delay(800);
}



void loop() {
  unsigned long now = millis();

  // ECG / AD8232 ----------------------------------------------------------
  if (now - lastECGRead >= ecgInterval) {
    lastECGRead = now;

    int lo1 = digitalRead(LO_PLUS);
    int lo2 = digitalRead(LO_MINUS);

    if (lo1 == 1 || lo2 == 1) {
      ecgConnected = false;
      ecgValue = -1;
      ecgValueGraph = -1;
      resetECGFilter();
    } else {
      ecgConnected = true;
      ecgValue = analogRead(ECG_PIN);

      ecgSum -= ecgBuffer[ecgBufferIndex];
      ecgBuffer[ecgBufferIndex] = ecgValue;
      ecgSum += ecgBuffer[ecgBufferIndex];

      ecgBufferIndex++;
      if (ecgBufferIndex >= ECG_FILTER_SIZE) {
        ecgBufferIndex = 0;
      }

      ecgValueGraph = ecgValue;
      ecgValueGraph = 512 + (ecgValue - (ecgSum / ECG_FILTER_SIZE))*5;
      delay(4);
    }
  }

  // MAX30102 / SpO2 + BPM ------------------------------------
  particleSensor.check();

  irValue = particleSensor.getIR();
  redValue = particleSensor.getRed();

  fingerDetected = (irValue > FINGER_THRESHOLD);

  if (fingerDetected) {
    shiftSpO2BuffersLeft(READ_BATCH);
    readNewSpO2Samples(READ_BATCH);
    computeSpO2();

    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      if (delta > 0) {
        beatsPerMinute = 60.0 / (delta / 1000.0);

        if (beatsPerMinute > 30 && beatsPerMinute < 220) {
          rates[rateSpot++] = (byte)beatsPerMinute;
          rateSpot %= RATE_SIZE;

          beatAvg = 0;
          for (byte i = 0; i < RATE_SIZE; i++) {
            beatAvg += rates[i];
          }
          beatAvg /= RATE_SIZE;
        }
      }
    }
  } else {
    resetMaxState();
  }
  // Respiracion / MPU6050 ------------------------------------
  if (now - lastRespSample >= RESP_SAMPLE_INTERVAL_MS) {
    lastRespSample = now;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    temperature = temp.temperature;

    float mag = sqrt(
      a.acceleration.x * a.acceleration.x +
      a.acceleration.y * a.acceleration.y +
      a.acceleration.z * a.acceleration.z
    );
    lastMag = mag;

    baseline = ema(baseline, mag, ALPHA_BASE);
    float detrended = mag - baseline;

    respSignal = ema(respSignal, detrended, ALPHA_RESP);
    ampTrack = ema(ampTrack, absf(respSignal), ALPHA_AMP);

    float threshold = ampTrack * 0.75;
    if (threshold < 0.015) threshold = 0.015;
    lastThreshold = threshold;

    if (!aboveThreshold) {
      if (respSignal > threshold) {
        aboveThreshold = true;
        peakValue = respSignal;
        peakTime = now;
      }
    } else {
      if (respSignal > peakValue) {
        peakValue = respSignal;
        peakTime = now;
      }

      if (respSignal < threshold * 0.55) {
        aboveThreshold = false;

        if ((lastBreathTime == 0) || (peakTime - lastBreathTime > MIN_BREATH_INTERVAL_MS)) {
          registerBreath(peakTime);
        }
      }
    }

    if (lastBreathTime != 0 && (now - lastBreathTime > 10000)) {
      resetRespiration();
    }
  }

  // Serial USB + Bluetooth -----------------------------------
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    Serial.print(ecgValueGraph);
    Serial.print(",");
    Serial.print(irValue);
    Serial.print(",");
    Serial.print(redValue);
    Serial.print(",");
    Serial.print(beatAvg);
    Serial.print(",");
    Serial.print(validSpO2 ? spo2 : -1);
    Serial.print(",");
    Serial.print(respSignal, 4);
    Serial.print(",");
    Serial.print(respRPM, 1);
    Serial.print(",");
    Serial.print(temperature, 1);
    Serial.print(" C");
    Serial.print(",");
    Serial.println(fingerDetected ? 1 : 0);

    // Solo ECG para la app
    BT.println(ecgValueGraph);
  }

  // OLED -----------------------------------------------------
  if (now - lastOLED >= oledInterval) {
    lastOLED = now;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("Bio System");

    display.setCursor(0, 10);
    display.print("ECG: ");
    if (ecgConnected) display.println(ecgValueGraph);
    else display.println("NO"); 

    display.setCursor(64, 10);
    display.print("BPM:");
    display.println(beatAvg);

    display.setCursor(0, 20);
    display.print("SpO2: ");
    if (fingerDetected && validSpO2) {
      display.print(spo2);
      display.println("%");
    } else {
      display.println("--");
    }

    display.setCursor(64, 20);
    display.print("IR:");
    display.println(fingerDetected ? "OK" : "NO");

    display.setCursor(0, 30);
    display.print("Resp: ");
    display.print(respRPM, 1);
    display.println("rpm");

    display.setCursor(0, 40);
    display.print("Finger: ");
    display.println(fingerDetected ? "YES" : "NOT");

    display.setCursor(0, 50);
    display.print("Temperature: ");
    display.print(temperature, 1);
    display.println(" C");



    display.display();
  }
}