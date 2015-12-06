




struct AquaponicsConfig {
  char ssid[32];
  char password[32];
  byte  dayStart;       // Hour of the day that the lamp starts
  byte  dayTime;        // Number of hours of daylight (LAMP ON)
  byte  pumpCycle;      // Time between 2 pump cycles
  byte  floodedTime;    // Time of water at high level
};

//EEPROM config
#define eeAddrSSID 0 // address in the EEPROM  for SSID
#define eeSizeSSID 32 // size in the EEPROM  for SSID
#define eeAddrPASS 32 // address in the EEPROM  for SSID
#define eeSizePASS 32 // size in the EEPROM  for SSID

enum waterLevel {
  DOWN = 0,
  FILLING = 1,
  UP = 2,
  CLEARING = 3
};

// Default time definition
#define DAY_TIME      14    // Number of hours of daylight (LAMP ON)
#define DAY_START     9     // Hour of the day that the day starts
#define PUMP_CYCLE    15    // Time between 2 pump cycles
#define WATER_UP_TIME 5     // Time to water at high level


