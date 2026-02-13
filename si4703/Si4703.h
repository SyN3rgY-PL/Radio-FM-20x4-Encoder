/*
 *  Si4703 FM Radio Receiver Library
 *
 *  Original work:
 *    Muthanna Alwahash (2020/21)
 *
 *  ESP32 / ESP32-S3 adaptation, I2C fixes, register map alignment,
 *  stability improvements and maintenance:
 *    SyN3rgY
 *
 *  Changes by SyN3rgY:
 *  - Correct shadow register ordering (0x0A..0x0F, 0x00..0x09) for proper I2C reads
 *  - Reliable 2-wire (I2C) initialization for ESP32 / ESP32-S3 on shared bus
 *  - Constructor defaults moved exclusively to header
 *  - Fixed STC/TUNE/SEEK behavior caused by incorrect register mapping
 *  - Improved compatibility with modern Arduino core for ESP32
 *  - Code cleanup and structural consistency
 *
 *  This version is intended for stable operation on ESP32 family boards
 *  with Si4703 breakout modules.
 *
 *  https://www.facebook.com/groups/esp32radio
 *
 *  Date: 01.02.2026
 */

#ifndef Si4703_h
#define Si4703_h

#include "Arduino.h"

// --------------------------- Default pins per architecture ---------------------------
// For ESP32-S3 Super Mini (your setup): I2C SDA=7, SCL=8, RST=9
#if defined(ARDUINO_ARCH_ESP32)
  #define SI4703_DEFAULT_RST  9
  #define SI4703_DEFAULT_SDA  7
  #define SI4703_DEFAULT_SCL  8
#else
  #define SI4703_DEFAULT_RST  4
  #define SI4703_DEFAULT_SDA  A4
  #define SI4703_DEFAULT_SCL  A5
#endif

//------------------------------------------------------------------------------------------------------------

// Band Select
static const uint8_t  	BAND_US_EU		= 0b00;	// 87.5–108 MHz (US / Europe, Default)
static const uint8_t  	BAND_JPW		= 0b01;	// 76–108 MHz (Japan wide band)
static const uint8_t  	BAND_JP			= 0b10;	// 76–90 MHz (Japan)

// De-emphasis
static const uint8_t	DE_75us			= 0b0;	// De-emphasis 75 μs. Used in USA (default)
static const uint8_t	DE_50us			= 0b1;	// De-emphasis 50 μs. Used in Europe, Australia, Japan.

// Channel Spacing (per Si4703 datasheet)
static const uint8_t  	SPACE_200KHz	= 0b00;	// 200 kHz (US / Australia)
static const uint8_t  	SPACE_100KHz 	= 0b01;	// 100 kHz (Europe / Japan)
static const uint8_t  	SPACE_50KHz  	= 0b10;	//  50 kHz (Other)

// GPIO1-3 Pins
static const uint8_t  	GPIO1			= 1;	// GPIO1
static const uint8_t  	GPIO2			= 2;	// GPIO2
static const uint8_t  	GPIO3			= 3;	// GPIO3

// GPIO1-3 Possible Values
static const uint8_t 	GPIO_Z			= 0b00;	// High impedance (default)
static const uint8_t 	GPIO_I			= 0b01;	// GPIO1-Reserved, GPIO2-STC/RDS int, or GPIO3-Mono/Sterio Indicator
static const uint8_t 	GPIO_Low		= 0b10;	// Low output (GND level)
static const uint8_t 	GPIO_High		= 0b11;	// High output (VIO level)

// Seek Mode
static const uint8_t 	SKMODE_WRAP		= 0b0;	// Wrap when reaching band limit
static const uint8_t 	SKMODE_STOP		= 0b1;	// Stop when reaching band limit

// Seek SNR Threshold
static const uint8_t 	SKSNR_DIS		= 0x0; // disabled (default) (Values are 0x0 to 0xF)
static const uint8_t 	SKSNR_MIN		= 0x1; // min value (most stops)
static const uint8_t 	SKSNR_MAX		= 0xF; // max value (fewest stops)

// Seek FM Impulse Detection Threshold
static const uint8_t 	SKCNT_DIS		= 0x0; // disabled (default) (Values are 0x0 to 0xF)
static const uint8_t 	SKCNT_MAX		= 0x1; // max value (most stops)
static const uint8_t 	SKCNT_MIN		= 0xF; // min value (fewest stops)

// Softmute Attenuation
static const uint8_t 	SMA_16dB		= 0b00;	// Softmute Attenuation 16dB (default)
static const uint8_t 	SMA_14dB		= 0b01;	// Softmute Attenuation 14dB
static const uint8_t 	SMA_12dB		= 0b10;	// Softmute Attenuation 12dB
static const uint8_t 	SMA_10dB		= 0b11;	// Softmute Attenuation 10dB

// Softmute Attack/Recover Rate
static const uint8_t 	SMRR_Fastest	= 0b00;	// Softmute Attack/Recover Rate = Fastest
static const uint8_t 	SMRR_Fast		= 0b01;	// Softmute Attack/Recover Rate = Fast
static const uint8_t 	SMRR_Slow		= 0b10;	// Softmute Attack/Recover Rate = Slow
static const uint8_t 	SMRR_Slowest	= 0b11;	// Softmute Attack/Recover Rate = Slowest

// Stereo/Mono Blend Level Adjustment
static const uint8_t 	BLA_31_49		= 0b00;	// 31–49 RSSI dBμV (default)
static const uint8_t 	BLA_37_55		= 0b01;	// 37–55 RSSI dBμV (+6 dB)
static const uint8_t 	BLA_19_37		= 0b10;	// 19–37 RSSI dBμV (–12 dB)
static const uint8_t 	BLA_25_43		= 0b11;	// 25–43 RSSI dBμV (–6 dB)

//------------------------------------------------------------------------------------------------------------

class Si4703
{
  public:
    Si4703(
      // MCU Pins Selection
      int rstPin  = SI4703_DEFAULT_RST,  // Reset Pin
      int sdioPin = SI4703_DEFAULT_SDA,  // I2C Data IO Pin (SDA)
      int sclkPin = SI4703_DEFAULT_SCL,  // I2C Clock Pin (SCL)
      int intPin  = 0,                   // Seek/Tune Complete and RDS interrupt Pin

      // Band Settings
      int band    = BAND_US_EU,          // Band Range
      int space   = SPACE_100KHz,        // Band Spacing
      int de      = DE_75us,             // De-Emphasis

      // Seek Settings
      int skmode  = SKMODE_STOP,         // Seek Mode
      int seekth  = 24,                  // Seek Threshold
      int skcnt   = SKCNT_MIN,           // Seek FM Impulse Detection Threshold (0x0..0xF)
      int sksnr   = SKSNR_MAX,           // Seek SNR Threshold (0x0..0xF)
      int agcd    = 0                    // AGC disable
    );

    void  powerUp();             // Power Up radio device
    void  powerDown();           // Power Down radio device to save power
    void  start();               // start radio

    int   getPN();               // Get DeviceID:Part Number
    int   getMFGID();            // Get DeviceID:Manufacturer ID
    int   getREV();              // Get ChipID:Chip Version
    int   getDEV();              // Get ChipID:Device
    int   getFIRMWARE();         // Get ChipID:Firmware Version

    int   getBandStart();        // Get Band Start Frequency
    int   getBandEnd();          // Get Band End Frequency
    int   getBandSpace();        // Get Band Spacing

    int   getRSSI(void);         // Get RSSI current value

    int   getChannel(void);      // Get current frequency (in 10kHz units, e.g. 8760 => 87.60MHz)
    int   setChannel(int freq);  // Set frequency (same unit)
    int   incChannel(void);      // Increment one band step
    int   decChannel(void);      // Decrement one band step

    int   seekUp(void);          // Seeks up and returns tuned channel or 0
    int   seekDown(void);        // Seeks down and returns tuned channel or 0

    void  setMono(bool en);      // 1=Force Mono
    bool  getMono(void);         // Get Mono status
    bool  getST(void);           // Get Stereo Status

    // NOTE: Si4703 DMUTE bit semantics: 1 = unmuted, 0 = muted.
    // This library keeps original naming: setMute(true) actually means DMUTE=1 (unmute).
    void  setMute(bool en);      // en maps to DMUTE bit (1=unmute, 0=mute)
    bool  getMute(void);         // returns DMUTE bit (1=unmute, 0=mute)

    void  setVolExt(bool en);    // Set Extended Volume Range
    bool  getVolExt(void);       // Get Extended Volume Range
    int   getVolume(void);       // Get current Volume value
    int   setVolume(int volume); // Sets volume value 0 to 15
    int   incVolume(void);       // Increment Volume
    int   decVolume(void);       // Decrement Volume

    void  readRDS(void);         // Reads RDS (TODO in this lib)

    void  writeGPIO(int GPIO,    // Write to GPIO1,GPIO2, and GPIO3
                    int val);    // values: GPIO_Z, GPIO_I, GPIO_Low, GPIO_High

  private:
    // MCU Pins Selection
    int _rstPin;
    int _sdioPin;
    int _sclkPin;
    int _intPin;

    // Band Settings
    int _band;
    int _space;
    int _de;
    int _bandStart;
    int _bandEnd;
    int _bandSpacing;

    // Seek Settings
    int _skmode;
    int _seekth;
    int _skcnt;
    int _sksnr;
    int _agcd;

    // Private Functions
    void  getShadow();
    byte  putShadow();
    void  bus3Wire(void);
    void  bus2Wire(void);
    void  setRegion(int band, int space, int de);
    bool  getSTC(void);
    int   seek(byte seekDir);

    // I2C interface
    static const int      I2C_ADDR     = 0x10;
    static const uint16_t I2C_FAIL_MAX = 10;

    static const uint16_t SEEK_DOWN = 0;
    static const uint16_t SEEK_UP   = 1;

    // Registers shadow
    union DEVICEID_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t MFGID :12;
        uint8_t  PN    :4;
      } bits;
    };

    union CHIPID_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t FIRMWARE:6;
        uint16_t DEV     :4;
        uint16_t REV     :6;
      } bits;
    };

    union POWERCFG_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t ENABLE :1;
        uint16_t        :5;
        uint16_t DISABLE:1;
        uint16_t        :1;
        uint16_t SEEK   :1;
        uint16_t SEEKUP :1;
        uint16_t SKMODE :1;
        uint16_t RDSM   :1;
        uint16_t        :1;
        uint16_t MONO   :1;
        uint16_t DMUTE  :1;
        uint16_t DSMUTE :1;
      } bits;
    };

    union CHANNEL_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t CHAN:10;
        uint16_t     :5;
        uint16_t TUNE:1;
      } bits;
    };

    union SYSCONFIG1_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t GPIO1  :2;
        uint16_t GPIO2  :2;
        uint16_t GPIO3  :2;
        uint16_t BLNDADJ:2;
        uint16_t        :2;
        uint16_t AGCD   :1;
        uint16_t DE     :1;
        uint16_t RDS    :1;
        uint16_t        :1;
        uint16_t STCIEN :1;
        uint16_t RDSIEN :1;
      } bits;
    };

    union SYSCONFIG2_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t VOLUME:4;
        uint16_t SPACE :2;
        uint16_t BAND  :2;
        uint16_t SEEKTH:8;
      } bits;
    };

    union SYSCONFIG3_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t SKCNT :4;
        uint16_t SKSNR :4;
        uint16_t VOLEXT:1;
        uint16_t       :3;
        uint16_t SMUTEA:2;
        uint16_t SMUTER:2;
      } bits;
    };

    union TEST1_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t       :14;
        uint16_t AHIZEN:1;
        uint16_t XOSCEN:1;
      } bits;
    };

    union TEST2_t
    {
      uint16_t word;
      struct bits { uint16_t :16; } bits;
    };

    union BOOTCONFIG_t
    {
      uint16_t word;
      struct bits { uint16_t :16; } bits;
    };

    union STATUSRSSI_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t RSSI :8;
        uint16_t ST   :1;
        uint16_t BLERA:2;
        uint16_t RDSS :1;
        uint16_t AFCRL:1;
        uint16_t SFBL :1;
        uint16_t STC  :1;
        uint16_t RDSR :1;
      } bits;
    };

    union READCHAN_t
    {
      uint16_t word;
      struct bits
      {
        uint16_t READCHAN:10;
        uint16_t BLERD   :2;
        uint16_t BLERC   :2;
        uint16_t BLERB   :2;
      } bits;
    };

    union RDSA_t { uint16_t word; struct bits { uint16_t RDSA:16; } bits; };
    union RDSB_t { uint16_t word; struct bits { uint16_t RDSB:16; } bits; };
    union RDSC_t { uint16_t word; struct bits { uint16_t RDSC:16; } bits; };
    union RDSD_t { uint16_t word; struct bits { uint16_t RDSD:16; } bits; };

    union shadow_t
    {
      uint16_t word[16];
      struct reg
      {
        STATUSRSSI_t STATUSRSSI; // 0x0A
        READCHAN_t   READCHAN;   // 0x0B
        RDSA_t       RDSA;       // 0x0C
        RDSB_t       RDSB;       // 0x0D
        RDSC_t       RDSC;       // 0x0E
        RDSD_t       RDSD;       // 0x0F
        DEVICEID_t   DEVICEID;   // 0x00
        CHIPID_t     CHIPID;     // 0x01
        POWERCFG_t   POWERCFG;   // 0x02
        CHANNEL_t    CHANNEL;    // 0x03
        SYSCONFIG1_t SYSCONFIG1; // 0x04
        SYSCONFIG2_t SYSCONFIG2; // 0x05
        SYSCONFIG3_t SYSCONFIG3; // 0x06
        TEST1_t      TEST1;      // 0x07
        TEST2_t      TEST2;      // 0x08
        BOOTCONFIG_t BOOTCONFIG; // 0x09
      } reg;
    } shadow;
};

#endif

