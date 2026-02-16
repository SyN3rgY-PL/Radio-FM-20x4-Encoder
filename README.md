# ESP32-S3 + Si4703 FM Radio + LCD 20x4 (I2C) + enkoder

Projekt prostego radia FM na **ESP32-S3-Zero** z układem **Si4703** oraz wyświetlaczem **LCD 20x4 I2C** (adres `0x27`).
Interfejs użytkownika jest utrzymany **1:1** jak w pierwotnej wersji (linie: `FREQ`, `SYG`, `TRYB`, `VOL`) z dodaną obsługą enkodera.

---
<p align="center">
  <a href="https://www.facebook.com/groups/esp32radio" target="_blank" rel="noopener noreferrer">
    <img src="https://yoradio.net/grupa-fm-512x512.png"
         alt="ESP32 Internet / FM Radio - PL">
  </a>
</p>
---

## Funkcje

- Strojenie FM (Si4703) oraz odczyt:
  - częstotliwości ("FREQ")
  - siły sygnału RSSI ("SYG")
  - trybu stereo/mono ("TRYB")
  - głośności ("VOL")
- Enkoder obrotowy:
  - **obrót** → zmiana częstotliwości (krok **0.1 MHz**)
  - **przycisk wciśnięty + obrót** → zmiana głośności (0–15)
- Odświeżanie UI:
  - szybka reakcja na enkoder
  - statusy (RSSI/stereo/vol) odświeżane co ~500 ms
- Poprawki stabilności UI:
  - linia `SYG` ograniczona do **dokładnie 20 znaków** (brak zawijania i nadpisywania `FREQ`)
  - `TRYB` wymuszane do wyświetlenia od razu po starcie (bez czekania na zmianę stanu)

---

## Sprzęt

- Płytka: **Waveshare ESP32-S3-Zero**
- Radio FM: **Si4703** (I2C)
- Wyświetlacz: **LCD 20x4 I2C** (typowo PCF8574), adres "0x27"
- Enkoder: dowolny enkoder z przyciskiem

---

## Połączenia

### I2C (wspólna magistrala dla LCD i Si4703)
- `GP7`  → `SDA`
- `GP8`  → `SCL`
- `GND`  → `GND`
- `3V3`  → zasilanie LCD/Si4703 (zgodnie z modułami)

### Si4703
- "RADIO_RST" → "GP6"
- "SDA/SCL"   → jak wyżej (GP7/GP8)

### Enkoder
- "ENC A / CLK" → "GP9"
- "ENC B / DT"  → "GP10"
- "ENC SW"      → "GP11"
- "GND"         → "GND"
- (opcjonalnie) "VCC" → "3V3"

Konfiguracja w kodzie:
- wejścia enkodera ustawione jako `INPUT_PULLUP` (stan aktywny LOW dla przycisku)

---

## Sterowanie

- **Kręcenie enkoderem**: zmiana częstotliwości w krokach **0.1 MHz**  
  (w kodzie: "FREQ_STEP = 10", czyli 10×10 kHz)
- **Przytrzymanie przycisku + kręcenie**: regulacja głośności (0–15)

---

## UI (LCD 20x4)

Wyświetlacz działa jak w oryginale:

- Linia 1: "ESP32 FM RADIO"
- Linia 2: "FREQ: xxx.xx MHz"
- Linia 3: "SYG: [##########] R:xx"  
  (10 segmentów + RSSI w formacie 2-cyfrowym, aby zawsze mieścić się w 20 kolumnach)
- Linia 4: "TRYB: STEREO/MONO" oraz "VOL:xx"

---

## Wymagane biblioteki

- `Wire` (Arduino)
- `LiquidCrystal_I2C`
- biblioteka do Si4703 (w kodzie: `Si4703.h`)

---

## Uwagi / diagnoza typowych problemów

1. **Ucięte napisy / znikające elementy UI**
   - Najczęściej wynikają z wypisywania >20 znaków w linii (LCD zawija i nadpisuje kolejne linie).
   - W tej wersji linia `SYG` jest ograniczona do 20 znaków, aby temu zapobiec.

2. **Brak `TRYB` po starcie**
   - Jeśli stan stereo jest taki sam jak cache, funkcja aktualizacji może nic nie wypisać.
   - Kod wymusza pierwsze rysowanie `TRYB` przez ustawienie `lastStereo` na wartość przeciwną do aktualnej.

3. **Enkoder “przeskakuje” / jest za czuły**
   - W "readEncoderDetent()" przyjęto standard: **4 przejścia = 1 detent**.
   - Jeśli Twój enkoder działa inaczej, próg można zmienić (np. 2 zamiast 4).

---

## Społeczność / wsparcie

Jeśli robisz podobne projekty albo chcesz dopytać o Si4703/ESP32, zajrzyj do grupy:

**ESP32 Internet / FM Radio - PL (Facebook):**
https://www.facebook.com/groups/esp32radio

---

## Licencja
