#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Si4703.h"

// ================= I2C (hub) =================
#define SDA_PIN 7
#define SCL_PIN 8

LiquidCrystal_I2C lcd(0x27, 20, 4);

// ================= RADIO =================
#define RADIO_RST 6
Si4703 radio(RADIO_RST);

// ================= ENKODER =================
#define ENC_A   9
#define ENC_B   10
#define ENC_SW  11

// Jednostki częstotliwości: 10240 = 102.40 MHz
#define FREQ_MIN   8750
#define FREQ_MAX   10800
#define FREQ_STEP  10      // 0.1 MHz = 10 * 10kHz

#define VOL_MIN 0
#define VOL_MAX 15

// ================= CACHE =================
int lastFreq = -1;
int lastRSSI = -1;
int lastVolume = -1;
bool lastStereo = false;

// Aktualny stan
int currentFreq = 10240;
int currentVol  = 8;

// ================= CUSTOM CHARS =================
byte barFull[8] = {
  B11111,B11111,B11111,B11111,
  B11111,B11111,B11111,B11111
};

byte barEmpty[8] = {
  B00000,B00000,B00000,B00000,
  B00000,B00000,B00000,B00000
};

void createCustomChars()
{
  lcd.createChar(0, barFull);
  lcd.createChar(1, barEmpty);
}

void drawStaticUI()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   CIGAR FM RADIO   ");
}

// ================= UI (1:1 jak oryginał) =================
void updateFrequency(int freq)
{
  if (freq == lastFreq) return;

  float mhz = freq / 100.0;

  lcd.setCursor(0, 1);
  lcd.print("FREQ: ");
  lcd.print(mhz, 2);
  lcd.print(" MHz   ");  // jak w oryginale (nie przekracza 20)

  lastFreq = freq;
}

void updateSignal()
{
  int rssi = radio.getRSSI();
  if (rssi == lastRSSI) return;

  int bars = constrain(map(rssi, 0, 75, 0, 10), 0, 10);

  // Linia 2 MUSI mieć max 20 znaków, żeby nic nie zawijało.
  // Układ:
  // "SYG: " (5) + 10 barów (10) = 15 kolumn
  // od kolumny 15: "R:" + dwie cyfry + spacja = 5 kolumn -> razem 20
  lcd.setCursor(0, 2);
  lcd.print("SYG: ");

  for (int i = 0; i < 10; i++)
    lcd.write(i < bars ? byte(0) : byte(1));

  lcd.setCursor(15, 2);
  lcd.print("R:");
  if (rssi < 10) lcd.print("0");
  lcd.print(rssi);
  lcd.print(" "); // dopełnienie do 20

  lastRSSI = rssi;
}

void updateStereo()
{
  bool stereo = radio.getST();
  if (stereo == lastStereo) return;

  lcd.setCursor(0, 3);
  lcd.print("TRYB: ");
  lcd.print(stereo ? "STEREO " : "MONO   "); // jak w oryginale

  lastStereo = stereo;
}

void updateVolume()
{
  int vol = radio.getVolume();
  if (vol == lastVolume) return;

  lcd.setCursor(13, 3);
  lcd.print("VOL:");
  if (vol < 10) lcd.print("0");
  lcd.print(vol);
  lcd.print(" ");

  lastVolume = vol;
}

// ================= ENCODER =================
bool readEncButtonHeld()
{
  static bool stable = false;
  static bool lastReading = false;
  static unsigned long lastChange = 0;

  bool reading = (digitalRead(ENC_SW) == LOW); // aktywne LOW

  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }
  if (millis() - lastChange > 30) {
    stable = reading;
  }
  return stable;
}

// -1/0/+1 na detent
int readEncoderDetent()
{
  static uint8_t last = 0x03; // pull-up => 11
  static int8_t acc = 0;

  uint8_t a = digitalRead(ENC_A) ? 1 : 0;
  uint8_t b = digitalRead(ENC_B) ? 1 : 0;
  uint8_t cur = (a << 1) | b;

  static const int8_t table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };

  uint8_t idx = (last << 2) | cur;
  int8_t move = table[idx];

  if (move) {
    acc += move;
    if (acc >= 4) { acc = 0; last = cur; return +1; }
    if (acc <= -4){ acc = 0; last = cur; return -1; }
  }

  last = cur;
  return 0;
}

void setFrequency(int freq)
{
  freq = constrain(freq, FREQ_MIN, FREQ_MAX);
  if (freq == currentFreq) return;

  currentFreq = freq;
  radio.setChannel(currentFreq);

  lastFreq = -1;
  updateFrequency(currentFreq);
}

void setVolume(int vol)
{
  vol = constrain(vol, VOL_MIN, VOL_MAX);
  if (vol == currentVol) return;

  currentVol = vol;
  radio.setVolume(currentVol);

  lastVolume = -1;
  updateVolume();
}

// ================= SETUP =================
void setup()
{
  Serial.begin(115200);
  delay(200);

  // Wymuszenie trybu I2C
  pinMode(RADIO_RST, OUTPUT);
  digitalWrite(RADIO_RST, LOW);
  delay(20);

  // Start I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  delay(50);

  digitalWrite(RADIO_RST, HIGH);
  delay(100);

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // LCD
  lcd.init();
  lcd.backlight();
  createCustomChars();
  drawStaticUI();

  delay(100);

  // Radio
  radio.start();
  delay(200);

  // Ustaw stan początkowy
  radio.setVolume(currentVol);
  radio.setChannel(currentFreq);

  // WYMUSZENIE PIERWSZEGO RYSOWANIA (żeby TRYB był od razu)
  lastFreq = -1;
  lastRSSI = -1;
  lastVolume = -1;
  lastStereo = !radio.getST();  // wymusza wejście do updateStereo()

  updateFrequency(radio.getChannel());
  updateSignal();
  updateStereo();
  updateVolume();
}

// ================= LOOP =================
void loop()
{
  bool volModeHeld = readEncButtonHeld();
  int det = readEncoderDetent();

  if (det != 0)
  {
    if (volModeHeld) setVolume(currentVol + det);
    else            setFrequency(currentFreq + det * FREQ_STEP);
  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 500)
  {
    updateFrequency(radio.getChannel());
    updateSignal();
    updateStereo();
    updateVolume();
    lastUpdate = millis();
  }
}
