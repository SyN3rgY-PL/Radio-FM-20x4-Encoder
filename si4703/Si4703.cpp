/*
 *  Si4703 FM Radio Receiver Library
 *  Original: Muthanna Alwahash 2020/21
 *
 *  Custom by: SyN3rgY
 *  - Default args ONLY here (.h)
 *  - ESP32-friendly I2C init assumed in .cpp
 *
 *  https://www.facebook.com/groups/esp32radio
 *
 *  Date: 01.02.2026
 */

#include "Arduino.h"
#include "Si4703.h"
#include "Wire.h"

// -----------------------------------------------------------------------------
// Si4703 Class Initialization
// NOTE: Default arguments must exist ONLY in the header (.h), not here.
// -----------------------------------------------------------------------------
Si4703::Si4703(
  // MCU Pins Selection
  int rstPin,    // Reset Pin
  int sdioPin,   // I2C Data IO Pin (SDA)
  int sclkPin,   // I2C Clock Pin (SCL)
  int intPin,    // Seek/Tune Complete and RDS interrupt Pin

  // Band Settings
  int band,      // Band Range
  int space,     // Band Spacing
  int de,        // De-Emphasis

  // Seek Settings
  int skmode,    // Seek Mode
  int seekth,    // Seek Threshold
  int skcnt,     // Seek Clicks Number Threshold
  int sksnr,     // Seek Signal/Noise Ratio
  int agcd       // AGC disable
)
{
  // MCU Pins Selection
  _rstPin  = rstPin;
  _sdioPin = sdioPin;
  _sclkPin = sclkPin;
  _intPin  = intPin;

  // Band Settings
  _band  = band;
  _space = space;
  _de    = de;

  // Seek Settings
  _skmode = skmode;
  _seekth = seekth;
  _skcnt  = skcnt;
  _sksnr  = sksnr;
  _agcd   = agcd;
}

// -----------------------------------------------------------------------------
// Read the entire register set (0x00 - 0x0F) to Shadow
// Reading order: 0A..0F, 00..09 = 16 words = 32 bytes
// -----------------------------------------------------------------------------
void Si4703::getShadow()
{
  Wire.requestFrom(I2C_ADDR, 32);
  for (int i = 0; i < 16; i++) {
    shadow.word[i] = (Wire.read() << 8) | Wire.read();
  }
}

// -----------------------------------------------------------------------------
// Write the current 6 control registers (0x02 to 0x07) to the Si4703
// (library uses i=8..13 -> reg 0x02..0x07)
// -----------------------------------------------------------------------------
byte Si4703::putShadow()
{
  Wire.beginTransmission(I2C_ADDR);
  for (int i = 8; i < 14; i++) {
    Wire.write(shadow.word[i] >> 8);
    Wire.write(shadow.word[i] & 0x00FF);
  }
  return Wire.endTransmission();
}

// -----------------------------------------------------------------------------
// 3-Wire Control Interface (SCLK, SEN, SDIO) - not implemented
// -----------------------------------------------------------------------------
void Si4703::bus3Wire(void)
{
  // TODO:
}

// -----------------------------------------------------------------------------
// 2-Wire Control Interface (SCL, SDA)
// Key note: breakout boards often pull SDIO high; to force 2-wire mode,
// SDIO must be LOW during reset, then released for I2C operation.
// -----------------------------------------------------------------------------
void Si4703::bus2Wire(void)
{
  pinMode(_rstPin, OUTPUT);
  pinMode(_sdioPin, OUTPUT);

  // Force 2-wire mode
  digitalWrite(_rstPin, LOW);
  digitalWrite(_sdioPin, LOW);
  delay(1);

  digitalWrite(_rstPin, HIGH);
  delay(1);

  // Release SDA line for shared I2C bus (important when LCD is on same bus)
  pinMode(_sdioPin, INPUT_PULLUP);

  // Start I2C
#if defined(ARDUINO_ARCH_ESP32)
  // On ESP32, always specify SDA/SCL to avoid falling back to default pins
  Wire.begin(_sdioPin, _sclkPin);
#else
  // Other MCUs usually take pins from hardware I2C mapping
  Wire.begin();
#endif
}

// -----------------------------------------------------------------------------
// Power Up Device
// -----------------------------------------------------------------------------
void Si4703::powerUp()
{
  // Enable Oscillator
  getShadow();
  shadow.reg.TEST1.bits.XOSCEN = 1;
  putShadow();
  delay(500);

  // Enable Device
  getShadow();
  shadow.reg.POWERCFG.bits.ENABLE  = 1;
  shadow.reg.POWERCFG.bits.DISABLE = 0;
  shadow.reg.POWERCFG.bits.DMUTE   = 1; // 1 = unmute (device default semantics)
  putShadow();
  delay(110);
}

// -----------------------------------------------------------------------------
// Power Down
// -----------------------------------------------------------------------------
void Si4703::powerDown()
{
  getShadow();
  shadow.reg.TEST1.bits.AHIZEN = 1;

  shadow.reg.SYSCONFIG1.bits.GPIO1 = GPIO_Z;
  shadow.reg.SYSCONFIG1.bits.GPIO2 = GPIO_Z;
  shadow.reg.SYSCONFIG1.bits.GPIO3 = GPIO_Z;

  shadow.reg.POWERCFG.bits.DMUTE   = 0; // mute
  shadow.reg.POWERCFG.bits.ENABLE  = 1;
  shadow.reg.POWERCFG.bits.DISABLE = 1;

  putShadow();
  delay(2);
}

// -----------------------------------------------------------------------------
// Start device in 2-wire mode and apply default config
// -----------------------------------------------------------------------------
void Si4703::start()
{
  bus2Wire();
  powerUp();

  getShadow();

  // Region band
  setRegion(_band, _space, _de);
  shadow.reg.SYSCONFIG2.bits.SPACE = _space;
  shadow.reg.SYSCONFIG2.bits.BAND  = _band;
  shadow.reg.SYSCONFIG1.bits.DE    = _de;

  // Tune
  shadow.reg.SYSCONFIG1.bits.STCIEN = 0;

  // Seek config
  shadow.reg.POWERCFG.bits.SEEK     = 0;
  shadow.reg.POWERCFG.bits.SEEKUP   = 1;
  shadow.reg.POWERCFG.bits.SKMODE   = _skmode;
  shadow.reg.SYSCONFIG2.bits.SEEKTH = _seekth;
  shadow.reg.SYSCONFIG3.bits.SKCNT  = _skcnt;
  shadow.reg.SYSCONFIG3.bits.SKSNR  = _sksnr;
  shadow.reg.SYSCONFIG1.bits.AGCD   = _agcd;

  // RDS
  shadow.reg.SYSCONFIG1.bits.RDSIEN = 0;
  shadow.reg.POWERCFG.bits.RDSM     = 0;
  shadow.reg.SYSCONFIG1.bits.RDS    = 1;

  // Audio
  shadow.reg.TEST1.bits.AHIZEN       = 0;
  shadow.reg.POWERCFG.bits.MONO      = 0;
  shadow.reg.SYSCONFIG1.bits.BLNDADJ = BLA_31_49;
  shadow.reg.SYSCONFIG2.bits.VOLUME  = 0;
  shadow.reg.SYSCONFIG3.bits.VOLEXT  = 0;

  // Softmute
  shadow.reg.POWERCFG.bits.DSMUTE    = 1;
  shadow.reg.SYSCONFIG3.bits.SMUTEA  = SMA_16dB;
  shadow.reg.SYSCONFIG3.bits.SMUTER  = SMRR_Fastest;

  // GPIOs
  shadow.reg.SYSCONFIG1.bits.GPIO1 = GPIO_Z;
  shadow.reg.SYSCONFIG1.bits.GPIO2 = GPIO_Z;
  shadow.reg.SYSCONFIG1.bits.GPIO3 = GPIO_Z;

  putShadow();
}

// -----------------------------------------------------------------------------
// Set FM Band Region limits and spacing
// -----------------------------------------------------------------------------
void Si4703::setRegion(int band, int space, int de)
{
  (void)de; // DE is applied elsewhere; kept for API compatibility

  switch (band)
  {
    case BAND_US_EU:      // 87.5–108 MHz
      _bandStart = 8750;
      _bandEnd   = 10800;
      break;

    case BAND_JPW:        // 76–108 MHz (Japan wide)
      _bandStart = 7600;
      _bandEnd   = 10800;
      break;

    case BAND_JP:         // 76–90 MHz (Japan)
      _bandStart = 7600;
      _bandEnd   = 9000;
      break;

    default:
      break;
  }

  switch (space)
  {
    case SPACE_100KHz:    // 100 kHz
      _bandSpacing = 10;  // in 10 kHz units -> 10 = 100 kHz
      break;

    case SPACE_200KHz:    // 200 kHz
      _bandSpacing = 20;  // in 10 kHz units -> 20 = 200 kHz
      break;

    case SPACE_50KHz:     // 50 kHz
      _bandSpacing = 5;   // in 10 kHz units -> 5 = 50 kHz
      break;

    default:
      break;
  }
}

// -----------------------------------------------------------------------------
// Mono control
// -----------------------------------------------------------------------------
void Si4703::setMono(bool en)
{
  getShadow();
  shadow.reg.POWERCFG.bits.MONO = en;
  putShadow();
}

bool Si4703::getMono(void)
{
  getShadow();
  return (shadow.reg.POWERCFG.bits.MONO);
}

// -----------------------------------------------------------------------------
// Audio mute control (note: DMUTE bit semantics: 1 = unmuted)
// This library keeps original API naming.
// -----------------------------------------------------------------------------
void Si4703::setMute(bool en)
{
  getShadow();
  shadow.reg.POWERCFG.bits.DMUTE = en;
  putShadow();
}

bool Si4703::getMute(void)
{
  getShadow();
  return (shadow.reg.POWERCFG.bits.DMUTE);
}

// -----------------------------------------------------------------------------
// Extended volume range
// -----------------------------------------------------------------------------
void Si4703::setVolExt(bool en)
{
  getShadow();
  shadow.reg.SYSCONFIG3.bits.VOLEXT = en;
  putShadow();
}

bool Si4703::getVolExt(void)
{
  getShadow();
  return (shadow.reg.SYSCONFIG3.bits.VOLEXT);
}

// -----------------------------------------------------------------------------
// Volume
// -----------------------------------------------------------------------------
int Si4703::getVolume(void)
{
  getShadow();
  return shadow.reg.SYSCONFIG2.bits.VOLUME;
}

int Si4703::setVolume(int volume)
{
  getShadow();

  if (volume < 0)  volume = 0;
  if (volume > 15) volume = 15;

  shadow.reg.SYSCONFIG2.bits.VOLUME = volume;
  putShadow();

  return getVolume();
}

int Si4703::incVolume(void)
{
  return setVolume(getVolume() + 1);
}

int Si4703::decVolume(void)
{
  return setVolume(getVolume() - 1);
}

// -----------------------------------------------------------------------------
// Channel / Frequency
// -----------------------------------------------------------------------------
int Si4703::getChannel()
{
  getShadow();
  // Freq = Spacing * Channel + Bottom of Band.
  return (_bandSpacing * shadow.reg.READCHAN.bits.READCHAN + _bandStart);
}

int Si4703::setChannel(int freq)
{
  if (freq > _bandEnd)   freq = _bandEnd;
  if (freq < _bandStart) freq = _bandStart;

  getShadow();
  shadow.reg.CHANNEL.bits.CHAN = (freq - _bandStart) / _bandSpacing;
  shadow.reg.CHANNEL.bits.TUNE = 1;
  putShadow();

  if (shadow.reg.SYSCONFIG1.bits.STCIEN == 0) {
    while (!getSTC()) {
      // wait
    }
  } else {
    // Interrupt-based STC not implemented
    // TODO:
  }

  getShadow();
  shadow.reg.CHANNEL.bits.TUNE = 0;
  putShadow();

  while (getSTC()) {
    // wait for clear
  }

  return getChannel();
}

int Si4703::incChannel(void)
{
  return setChannel(getChannel() + _bandSpacing);
}

int Si4703::decChannel(void)
{
  return setChannel(getChannel() - _bandSpacing);
}

// -----------------------------------------------------------------------------
// STC status
// -----------------------------------------------------------------------------
bool Si4703::getSTC(void)
{
  getShadow();
  return shadow.reg.STATUSRSSI.bits.STC;
}

// -----------------------------------------------------------------------------
// Seek
// -----------------------------------------------------------------------------
int Si4703::seek(byte seekDirection)
{
  getShadow();
  shadow.reg.POWERCFG.bits.SEEKUP = seekDirection;
  shadow.reg.POWERCFG.bits.SEEK   = 1;
  putShadow();

  if (shadow.reg.SYSCONFIG1.bits.STCIEN == 0) {
    while (!getSTC()) {
      delay(40);
      // seek progress optional
    }
  } else {
    // Interrupt-based STC not implemented
    // TODO:
  }

  getShadow();
  bool sfbl = shadow.reg.STATUSRSSI.bits.SFBL;

  shadow.reg.POWERCFG.bits.SEEK = 0;
  putShadow();

  while (getSTC()) {
    // wait
  }

  if (sfbl) return 0; // failure/band limit
  return getChannel();
}

int Si4703::seekUp()
{
  return seek(SEEK_UP);
}

int Si4703::seekDown()
{
  return seek(SEEK_DOWN);
}

// -----------------------------------------------------------------------------
// Stereo indicator
// -----------------------------------------------------------------------------
bool Si4703::getST(void)
{
  getShadow();
  return shadow.reg.STATUSRSSI.bits.ST;
}

// -----------------------------------------------------------------------------
// RDS
// -----------------------------------------------------------------------------
void Si4703::readRDS(void)
{
  // TODO:
}

// -----------------------------------------------------------------------------
// GPIO write
// -----------------------------------------------------------------------------
void Si4703::writeGPIO(int GPIO, int val)
{
  getShadow();

  switch (GPIO)
  {
    case GPIO1: shadow.reg.SYSCONFIG1.bits.GPIO1 = val; break;
    case GPIO2: shadow.reg.SYSCONFIG1.bits.GPIO2 = val; break;
    case GPIO3: shadow.reg.SYSCONFIG1.bits.GPIO3 = val; break;
    default: break;
  }

  putShadow();
}

// -----------------------------------------------------------------------------
// Device info
// -----------------------------------------------------------------------------
int Si4703::getPN()
{
  getShadow();
  return shadow.reg.DEVICEID.bits.PN;
}

int Si4703::getMFGID()
{
  getShadow();
  return shadow.reg.DEVICEID.bits.MFGID;
}

int Si4703::getREV()
{
  getShadow();
  return shadow.reg.CHIPID.bits.REV;
}

int Si4703::getDEV()
{
  getShadow();
  return shadow.reg.CHIPID.bits.DEV;
}

int Si4703::getFIRMWARE()
{
  getShadow();
  return shadow.reg.CHIPID.bits.FIRMWARE;
}

// -----------------------------------------------------------------------------
// Band info getters
// -----------------------------------------------------------------------------
int Si4703::getBandStart()
{
  return _bandStart;
}

int Si4703::getBandEnd()
{
  return _bandEnd;
}

int Si4703::getBandSpace()
{
  return _bandSpacing;
}

// -----------------------------------------------------------------------------
// RSSI
// -----------------------------------------------------------------------------
int Si4703::getRSSI(void)
{
  getShadow();
  return shadow.reg.STATUSRSSI.bits.RSSI;
}

