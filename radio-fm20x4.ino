#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include "src/Si4703.h"

// ================= LOGI =================
#define LOG_BAUD 115200
#define LOGI(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[ERR ] " fmt "\n", ##__VA_ARGS__)

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

// ================= PREFERENCES / NVS =================
Preferences prefs;
static const char* PREF_NS   = "fmradio";
static const char* PREF_FREQ = "freq";
static const char* PREF_VOL  = "vol";

bool settingsDirty = false;
unsigned long lastUserChangeMs = 0;
const unsigned long SAVE_DELAY_MS = 1500;  // zapis po chwili bez kręcenia

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

// ================= NARZĘDZIA =================
bool i2cDevicePresent(uint8_t address)
{
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

void scanI2C()
{
  LOGI("Skan I2C...");
  bool foundAny = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      LOGI("Znaleziono urzadzenie I2C pod adresem 0x%02X", addr);
      foundAny = true;
    }
  }

  if (!foundAny) {
    LOGW("Nie wykryto zadnych urzadzen I2C");
  }
}

bool loadSettings()
{
  if (!prefs.begin(PREF_NS, true)) {
    LOGE("Nie mozna otworzyc Preferences do odczytu");
    return false;
  }

  int savedFreq = prefs.getInt(PREF_FREQ, currentFreq);
  int savedVol  = prefs.getInt(PREF_VOL, currentVol);
  prefs.end();

  bool corrected = false;

  if (savedFreq < FREQ_MIN || savedFreq > FREQ_MAX) {
    LOGW("Zapisana czestotliwosc poza zakresem: %d -> przywracam domyslna %d",
         savedFreq, currentFreq);
    savedFreq = currentFreq;
    corrected = true;
  }

  if (savedVol < VOL_MIN || savedVol > VOL_MAX) {
    LOGW("Zapisana glosnosc poza zakresem: %d -> przywracam domyslna %d",
         savedVol, currentVol);
    savedVol = currentVol;
    corrected = true;
  }

  currentFreq = savedFreq;
  currentVol  = savedVol;

  if (!corrected) {
    LOGI("Wczytano ustawienia: stacja=%d (%.2f MHz), vol=%d",
         currentFreq, currentFreq / 100.0, currentVol);
  }

  return true;
}

bool saveSettingsNow()
{
  if (!prefs.begin(PREF_NS, false)) {
    LOGE("Nie mozna otworzyc Preferences do zapisu");
    return false;
  }

  size_t f = prefs.putInt(PREF_FREQ, currentFreq);
  size_t v = prefs.putInt(PREF_VOL, currentVol);
  prefs.end();

  if (f == 0 || v == 0) {
    LOGE("Blad zapisu ustawien do NVS");
    return false;
  }

  LOGI("Zapisano ustawienia: stacja=%d (%.2f MHz), vol=%d",
       currentFreq, currentFreq / 100.0, currentVol);

  settingsDirty = false;
  return true;
}

void markSettingsDirty()
{
  settingsDirty = true;
  lastUserChangeMs = millis();
}

// ================= BEZPIECZNE ODCZYTY =================
int safeGetChannel()
{
  int ch = radio.getChannel();
  static unsigned long lastWarn = 0;

  if (ch < FREQ_MIN || ch > FREQ_MAX) {
    if (millis() - lastWarn > 2000) {
      LOGW("Nieprawidlowy odczyt kanalu z radia: %d, uzywam currentFreq=%d", ch, currentFreq);
      lastWarn = millis();
    }
    return currentFreq;
  }
  return ch;
}

int safeGetVolume()
{
  int vol = radio.getVolume();
  static unsigned long lastWarn = 0;

  if (vol < VOL_MIN || vol > VOL_MAX) {
    if (millis() - lastWarn > 2000) {
      LOGW("Nieprawidlowy odczyt glosnosci z radia: %d, uzywam currentVol=%d", vol, currentVol);
      lastWarn = millis();
    }
    return currentVol;
  }
  return vol;
}

int safeGetRSSI()
{
  int rssi = radio.getRSSI();
  static unsigned long lastWarn = 0;

  if (rssi < 0 || rssi > 127) {
    if (millis() - lastWarn > 2000) {
      LOGW("Podejrzany RSSI: %d, ograniczam do zakresu 0..127", rssi);
      lastWarn = millis();
    }
    rssi = constrain(rssi, 0, 127);
  }
  return rssi;
}

bool safeGetStereo()
{
  return radio.getST();
}

// ================= LCD =================
void createCustomChars()
{
  lcd.createChar(0, barFull);
  lcd.createChar(1, barEmpty);
}

void drawStaticUI()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   ESP32 FM RADIO   ");
}

// ================= UI (1:1 jak oryginał) =================
void updateFrequency(int freq)
{
  if (freq == lastFreq) return;

  float mhz = freq / 100.0;

  lcd.setCursor(0, 1);
  lcd.print("FREQ: ");
  lcd.print(mhz, 2);
  lcd.print(" MHz   ");  // jak w oryginale

  lastFreq = freq;
}

void updateSignal()
{
  int rssi = safeGetRSSI();
  if (rssi == lastRSSI) return;

  int bars = constrain(map(rssi, 0, 75, 0, 10), 0, 10);

  // Linia 2 max 20 znaków
  lcd.setCursor(0, 2);
  lcd.print("SYG: ");

  for (int i = 0; i < 10; i++)
    lcd.write(i < bars ? byte(0) : byte(1));

  lcd.setCursor(15, 2);
  lcd.print("R:");
  if (rssi < 10) lcd.print("0");
  lcd.print(rssi);
  lcd.print(" ");

  lastRSSI = rssi;
}

void updateStereo()
{
  bool stereo = safeGetStereo();
  if (stereo == lastStereo) return;

  lcd.setCursor(0, 3);
  lcd.print("TRYB: ");
  lcd.print(stereo ? "STEREO " : "MONO   "); // jak w oryginale

  lastStereo = stereo;
}

void updateVolume()
{
  int vol = safeGetVolume();
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

  LOGI("Ustawiono stacje: %d (%.2f MHz)", currentFreq, currentFreq / 100.0);

  lastFreq = -1;
  updateFrequency(currentFreq);
  markSettingsDirty();
}

void setVolume(int vol)
{
  vol = constrain(vol, VOL_MIN, VOL_MAX);
  if (vol == currentVol) return;

  currentVol = vol;
  radio.setVolume(currentVol);

  LOGI("Ustawiono glosnosc: %d", currentVol);

  lastVolume = -1;
  updateVolume();
  markSettingsDirty();
}

// ================= SETUP =================
void setup()
{
  Serial.begin(LOG_BAUD);
  delay(200);

  LOGI("====================================");
  LOGI("Start ESP32 FM RADIO");
  LOGI("UART: %d", LOG_BAUD);
  LOGI("Domyslne ustawienia: stacja=%.2f MHz, vol=%d", currentFreq / 100.0, currentVol);
  LOGI("====================================");

  // Wczytaj zapisane ustawienia usera
  loadSettings();

  // Wymuszenie trybu I2C
  pinMode(RADIO_RST, OUTPUT);
  digitalWrite(RADIO_RST, LOW);
  delay(20);

  // Start I2C
  LOGI("Start I2C: SDA=%d, SCL=%d", SDA_PIN, SCL_PIN);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  delay(50);

  scanI2C();

  if (!i2cDevicePresent(0x27)) {
    LOGE("LCD 0x27 nie odpowiada na I2C");
  } else {
    LOGI("LCD 0x27 wykryty poprawnie");
  }

  digitalWrite(RADIO_RST, HIGH);
  delay(100);

  // Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  LOGI("Encoder gotowy: A=%d B=%d SW=%d", ENC_A, ENC_B, ENC_SW);

  // LCD
  lcd.init();
  lcd.backlight();
  createCustomChars();
  drawStaticUI();
  LOGI("LCD zainicjalizowany");

  delay(100);

  // Radio
  LOGI("Uruchamianie Si4703...");
  radio.start();
  delay(200);

  // Ustaw stan początkowy
  radio.setVolume(currentVol);
  radio.setChannel(currentFreq);

  LOGI("Przywrocono ustawienia po starcie: %.2f MHz, vol=%d",
       currentFreq / 100.0, currentVol);

  // WYMUSZENIE PIERWSZEGO RYSOWANIA
  lastFreq = -1;
  lastRSSI = -1;
  lastVolume = -1;
  lastStereo = !safeGetStereo();  // wymusza wejście do updateStereo()

  updateFrequency(safeGetChannel());
  updateSignal();
  updateStereo();
  updateVolume();

  // Diagnostyka po starcie
  int bootCh = safeGetChannel();
  int bootVol = safeGetVolume();
  int bootRssi = safeGetRSSI();
  bool bootSt = safeGetStereo();

  LOGI("Stan radia po starcie:");
  LOGI("  Kanal : %d (%.2f MHz)", bootCh, bootCh / 100.0);
  LOGI("  Volume: %d", bootVol);
  LOGI("  RSSI  : %d", bootRssi);
  LOGI("  Tryb  : %s", bootSt ? "STEREO" : "MONO");
}

// ================= LOOP =================
void loop()
{
  bool volModeHeld = readEncButtonHeld();
  int det = readEncoderDetent();

  if (det != 0)
  {
    if (volModeHeld) setVolume(currentVol + det);
    else             setFrequency(currentFreq + det * FREQ_STEP);
  }

  // Auto-zapis po chwili od ostatniej zmiany
  if (settingsDirty && (millis() - lastUserChangeMs > SAVE_DELAY_MS))
  {
    saveSettingsNow();
  }

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 500)
  {
    updateFrequency(safeGetChannel());
    updateSignal();
    updateStereo();
    updateVolume();
    lastUpdate = millis();
  }
}
