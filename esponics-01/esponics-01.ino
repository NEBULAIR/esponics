
/*
 *  First implementation of the aaquaponics project on esp8266 with arduino
 *  - Get information from DHT sensor
 *  - Logging on thingspeak
 *  - The use of timers allows to optimize and creat time based environment
 *  - Configuration from serial link
 *  - Configuration saved in eeprom
 *  
 *
 *  Developed from :
 *    https://github.com/atwc/ESP8266-arduino-demos
 *  Use the librairies
 *  - DHT
 *  - Timer in user_interface
 *  
 *  Serial Link commandes is composed by one letter and the value : YXXXXXXX
 *  Commande detail :
 *  - L > change start time of LAMP
 *  - D > change day time duration of LAMP
 *  - R > change pump flooding frequence
 *  - F > change flooding duration
 *  - P > Change the wifi pasword
 *  - S > Change the SSID
 *  - W > Write parameters in eeprom"
 *  - T > Reset timing
 *  - H > Change thingspeak channel
 *  
 *  
 *  TODO
 *  - Add thingspeak log on test channel
 *  - Add NTP time read function 
 *  - Add config from internet
 *  - Use a table for thingspeak inputs
 *  - Check for use of hardware interrupt for water sensor, May no be needed
 *  
 */

#include "hardware_def.h"
#include "aquaponics.h"

#include <ESP8266WiFi.h>
#include <DHT.h>
#include <EEPROM.h>

extern "C" {
#include "user_interface.h"
}

#define DEBUG 1 //Use DEBUG to reduce the time 1 minute = 5s

//Serial Config
#define BAUDRATE 115200  // Serial link speed

//Timer elements
os_timer_t myTimer1;
os_timer_t myTimer2;
os_timer_t myTimer3;
// boolean to activate timer events
bool tick1Occured;
bool tick2Occured;
bool tick3Occured;
// Need to have a global address for the timer id
const char tickId1=1;
const char tickId2=2;
const char tickId3=3;
// Functions declaration
void timersSetup(void);
void timerCallback(void *);
void timerInit(os_timer_t *pTimerPointer, uint32_t milliSecondsValue, os_timer_func_t *pFunction, char *pId);

//Wifi config
//Function declarations
void wifiConnect(void);
//Wifi Global variables
WiFiClient client;
#define MAX_CONNEXION_TRY 50

//Function declarations
void thingSpeakWrite (String, unsigned long, float, float, float, float, float, float, float);

//Sensors config
DHT dht(DHTPIN, DHT22,15);

//Application config
AquaponicsConfig conf;
unsigned long getMacAddress(void);
void ioInits(void);
void printInfo(void);
void waterControl(void);

//Application values
float temperature;
float humidity;
byte waterLevelState = 0;     // water level status memory 

// Time counter variables
volatile int hoursCounter;    // 0 to 23
volatile int secondsCounter;  // 0 to 59
volatile int minutesCounter;  // 0 to 59

//EEPROM Function declarations
void eepromWrite(void);
void eepromRead(void);
//EEPROM Global variables
bool needSave;

// Serial string management variables
void executeCommand();
void serialStack();
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

/* void setup(void) 
 *  Setup software run only once
 *  
*/
void setup() {
  
  //Start Serial
  Serial.begin(BAUDRATE);
  Serial.println("");
  Serial.println("--------------------------");
  Serial.println("    ESP8266 Full Test     ");
  Serial.println("--------------------------");
 
  timersSetup();
  ioInits();

  conf.mac = getMacAddress();
  eepromRead();

  hoursCounter = DAY_START;           // Init the time to DAY_START at startup
  minutesCounter = 1;
  digitalWrite(LAMP, HIGH);

  printInfo();
  Serial.println("------");
  
  //Init wifi and web server
  wifiConnect();

  //Init DHT temperature and Himidity reading
  dht.begin();
}

/* void loop(void) 
 *  Main program automatically loaded
 *  
*/
void loop() {
  
  //Check if a timer occured to execute the action
  //Timer 1 action every seconds
  if (tick1Occured == true){
    tick1Occured = false;
    //Toggle LED
    digitalWrite(RED_LED, !digitalRead(RED_LED));
  }
  
  //Check if eeprom parameters need to be saved
  if (needSave){
    needSave = 0;
    eepromWrite();
  }
  
  //Check for serial input
  serialStack();
  //Execute recived command
  executeCommand();
  
  //Timer 2 action every minutes
  if (tick2Occured == true){
    tick2Occured = false;
    minutesCounter ++;      //Increment the minutes counter
    // Every  60min increment the hours counter
    if (0 == minutesCounter % 60)
    {
      hoursCounter ++;        // Increments the hours counter
      minutesCounter = 0;     // Clears the minutes counter
      
      // At the end of the day reset the hours counter
      if (23 < hoursCounter)
      {
        hoursCounter = 0;     // Clear the hours counter
      }
  
      // Execute following code once every hour
      //Check if the lamp has to be turn on or off
      if (conf.dayStart == hoursCounter)
        digitalWrite(LAMP, 1);
      if ((conf.dayStart + conf.dayTime) == hoursCounter)
        digitalWrite(LAMP, 0);
    }
    printInfo();
  }
  
  //Timer 3 action every 10mn
  if (tick3Occured == true){
    tick3Occured = false;
    //Temperature and Humidity Reading
    humidity =    dht.readHumidity();
    temperature = dht.readTemperature();
    thingSpeakWrite ( conf.thingspeakApi, conf.mac, temperature, humidity, NAN,
                      conf.dayStart, conf.dayTime, conf.pumpFreq, conf.floodedTime);
  }

  waterControl();
 
  //Give th time to th os to do his things
  yield();  // or delay(0);
}

/* void timerSetup(void *pArg)
 *  Setup all timers 
 *  
 *  Input  : 
 *  Output :
*/
void timersSetup(void)
{
  //Init and start timers
  tick1Occured = false;
  tick2Occured = false;
  tick3Occured = false;
 
  if(DEBUG)
  { //Reduce timing for test and debug
    timerInit(&myTimer1, 1000,  timerCallback, (char*)&tickId1);
    timerInit(&myTimer2, 1000*5,  timerCallback, (char*)&tickId2);
    timerInit(&myTimer3, 1000*60,  timerCallback, (char*)&tickId3);
  }
  else
  { //Normal timing
    timerInit(&myTimer1, TIMER1,  timerCallback, (char*)&tickId1);
    timerInit(&myTimer2, TIMER2,  timerCallback, (char*)&tickId2);
    timerInit(&myTimer3, TIMER3,  timerCallback, (char*)&tickId3);
  }
}

/* void timerCallback(void *pArg)
 *  Function called by the os_timers at every execution
 *  Only one function is used for all timers, the timer id comm in the pArg
 *  
 *  Input  : 
 *  Output :
*/
void timerCallback(void *pArg) {
  
  char timerId = *(char*)pArg; //Value inside (*) of pArg, casted into a char pointer
  
  switch (timerId){
    case 1 :
      tick1Occured = true;
      break;
    case 2 :
      tick2Occured = true;
      break;
    case 3 :
      tick3Occured = true;
      break;
    default :
      //Nothings to do
      break;
  }
} 

/* timerInit(os_timer_t *pTimerPointer, uint32_t milliSecondsValue, os_timer_func_t *pFunction, char *pId) 
 *  Start and init all timers 
 *  
 *  Input  : 
 *  Output :
*/
void timerInit(os_timer_t *pTimerPointer, uint32_t milliSecondsValue, os_timer_func_t *pFunction, char *pId) {
   /*
    Maximum 7 timers
    os_timer_setfn - Define a function to be called when the timer fires
    void os_timer_setfn(os_timer_t *pTimer, os_timer_func_t *pFunction, void *pArg)
    
    Define the callback function that will be called when the timer reaches zero. 
    The pTimer parameters is a pointer to the timer control structure.
    The pFunction parameters is a pointer to the callback function.
    The pArg parameter is a value that will be passed into the called back function. 
    The callback function should have the signature: void (*functionName)(void *pArg)
    The pArg parameter is the value registered with the callback function.
  */
  os_timer_setfn(pTimerPointer, pFunction, pId);
  /*
    os_timer_arm -  Enable a millisecond granularity timer.
    void os_timer_arm( os_timer_t *pTimer, uint32_t milliseconds, bool repeat)
  
    Arm a timer such that is starts ticking and fires when the clock reaches zero.
    The pTimer parameter is a pointed to a timer control structure.
    The milliseconds parameter is the duration of the timer measured in milliseconds. 
    The repeat parameter is whether or not the timer will restart once it has reached zero.
  */
  os_timer_arm(pTimerPointer, milliSecondsValue, true);
} 

/* void wifiConnect(void)
 *  Try to connect to the wifi, stop after a time without success
 *  
 *  Input  : None
 *  Output : None
*/
void wifiConnect(void){
  
  int wifiErrorCount;
  
  // Connect to WiFi network
  Serial.println();
  Serial.println("Connecting to ");
  Serial.println(conf.ssid);
  Serial.println("Connecting to ");
  Serial.println(conf.password);

  WiFi.begin(conf.ssid, conf.password);
  
  wifiErrorCount = 0;
  while (WiFi.status() != WL_CONNECTED and wifiErrorCount < MAX_CONNEXION_TRY )
  {
    delay(500);
    Serial.print(".");
    wifiErrorCount++;
  }
  Serial.println("");
  if (wifiErrorCount < MAX_CONNEXION_TRY)
  {
    Serial.println("WiFi connected");
  }
  else
  {
    Serial.println("WiFi not connected");
  }
}

/* void thingSpeakWrite(void)
 *  Write data to thingspeak server 
 *  TODO Use a table as input
 *  
 *  Input  : APIKey - the write api key from the channel
 *           fieldX - every channel values or NAN if not used
 *  Output :
*/
void thingSpeakWrite (String APIKey,
                      unsigned long field1, float field2, float field3, float field4,
                      float field5, float field6, float field7, float field8)
{
  
  const char* thingspeakServer = "api.thingspeak.com";

  if (client.connect(thingspeakServer,80)) {  //   "184.106.153.149" or api.thingspeak.com
    String postStr = APIKey;
    if (!isnan(field1))
    {
      postStr +="&field1=";
      postStr += String(field1);
    }
    if (!isnan(field2))
    {
      postStr +="&field2=";
      postStr += String(field2);
    }
    if (!isnan(field3))
    {
      postStr +="&field3=";
      postStr += String(field3);
    }
    if (!isnan(field4))
    {
      postStr +="&field4=";
      postStr += String(field4);
    }
    if (!isnan(field5))
    {
      postStr +="&field5=";
      postStr += String(field5);
    }
    if (!isnan(field6))
    {
      postStr +="&field6=";
      postStr += String(field6);
    }
    if (!isnan(field7))
    {
      postStr +="&field7=";
      postStr += String(field7);
    }
    if (!isnan(field8))
    {
      postStr +="&field8=";
      postStr += String(field8);
    }
    postStr += "\r\n\r\n";
 
     client.print("POST /update HTTP/1.1\n");
     client.print("Host: api.thingspeak.com\n");
     client.print("Connection: close\n");
     client.print("X-THINGSPEAKAPIKEY: "+String(conf.thingspeakApi)+"\n");
     client.print("Content-Type: application/x-www-form-urlencoded\n");
     client.print("Content-Length: ");
     client.print(postStr.length());
     client.print("\n\n");
     client.print(postStr);
 
  }
  client.stop();
}

/* void printInfo(void)
 *  Print all application info on the serial link 
 *  
 *  Input  : 
 *  Output :
*/
void printInfo(void)
{
    Serial.print("MAC : ");
    Serial.println(String(conf.mac,HEX));
    Serial.print("Time : ");
    Serial.print(hoursCounter);
    Serial.print(":");
    Serial.println(minutesCounter);
    Serial.print("Water level : ");
    Serial.println(waterLevelState);

    Serial.print("Flood every ");
    Serial.print(conf.pumpFreq);
    Serial.print("mn for ");
    Serial.print(conf.floodedTime);
    Serial.println("mn.");
    
    Serial.print("Start the lamp at ");
    Serial.print(conf.dayStart);
    Serial.print("h for ");
    Serial.print(conf.dayTime);
    Serial.println("h.");

    Serial.print("Temperature : ");
    Serial.print(temperature);
    Serial.print(" - Humidity : ");
    Serial.println(humidity);
}

/* void ioInits(void)
 *  Init all Inputs and Outputs for the application 
 *  
 *  Input  : 
 *  Output :
*/
void ioInits(void)
{
  //Init leds
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(BLUE_LED, HIGH);
  digitalWrite(RED_LED, HIGH);
  
  // Setup the pin function
  pinMode(LAMP, OUTPUT);
  pinMode(LAMP, OUTPUT);
  pinMode(PUMP_IN, OUTPUT);
  pinMode(PUMP_OUT, OUTPUT);
  pinMode(WATER_UP, INPUT_PULLUP);
  pinMode(WATER_DOWN, INPUT_PULLUP);
}

/* void waterControl(void)
 *  Control the pumps depending of the actual water state 
 *  
 *  Input  : 
 *  Output :
*/
void waterControl(void)
{
  switch (waterLevelState) {
    
    case DOWN:
        //Wait for pump frequence time to activate pump
        if(0 == (minutesCounter % PUMP_FREQ))
        {
          digitalWrite(PUMP_IN, 1);   // Turn ON the filling pump
          Serial.println("Turn ON the finning pump");
          waterLevelState = FILLING;
        }
      break;
      
    case FILLING:
       // wait for water up sensor to be activated
       if(0 == digitalRead(WATER_UP))
        {
          digitalWrite(PUMP_IN, 0); // Turn OFF the filling pump
          Serial.println("Turn OFF the finning pump");
          waterLevelState = UP;
        }
      break;
      
    case UP:
        //Wait for level up time passed to clear 
        if(0 == ((minutesCounter % PUMP_FREQ) % WATER_UP_TIME))
        {
          digitalWrite(PUMP_OUT, 1);   // Turn ON the clearing pump
          Serial.println("Turn ON the clearing pump");
          waterLevelState = CLEARING;
        }
      break;
      break;
      
    case CLEARING:
        // wait for water down sensor to be activated
        if(0 == digitalRead(WATER_DOWN))
        {
          digitalWrite(PUMP_OUT, 0); // Turn OFF the clearing pump
          Serial.println("Turn OFF the clearing pump");
          waterLevelState = DOWN;
        }
      break;
      
    default:
      // default is optional
    break;
  }
}

/* void eepromWrite(void)
 *  Write all application parameters in eeprom 
 *  
 *  Input  : 
 *  Output :
*/
void eepromWrite(void)
{
  char letter;
  int i, addr;
  //Activate eeprom
  EEPROM.begin(512);

  Serial.println("Save application parameters in eeprom");

  // save wifi ssid in eeprom
  addr = eeAddrSSID;
  for (i = 0 ; i < eeSizeSSID ; i++)
  { 
    EEPROM.write(addr, conf.ssid[i]);
    if('\0' == conf.ssid[i])
      break;
    addr++;
  }
  // save wifi password in eeprom
  addr = eeAddrPASS;
  for (i = 0 ; i < eeSizePASS ; i++)
  {
    EEPROM.write(addr, conf.password[i]);
    if('\0' == conf.password[i])
      break;
    addr++;
  }
  // save thingspeak api in eeprom
  addr = eeAddrTSAPI;
  for (i = 0 ; i < eeSizeTSAPI ; i++)
  {
    EEPROM.write(addr, conf.thingspeakApi[i]);
    if('\0' == conf.thingspeakApi[i])
      break;
    addr++;
  }

  EEPROM.write(eeAddrDayStart, conf.dayStart);     // Hour of the day that the lamp starts
  EEPROM.write(eeAddrDayTime, conf.dayTime);      // Number of hours of daylight (LAMP ON)
  EEPROM.write(eeAddrPumpFreq, conf.pumpFreq);    // Time between 2 pump cycles
  EEPROM.write(eeAddrFloodedTime, conf.floodedTime);  // Time of water at high level
  
  EEPROM.end();
}

/* void eepromRead(void)
 *  Read all application parameters from eeprom 
 *  
 *  Input  : 
 *  Output :
*/
void eepromRead(void)
{
  char letter;
  int i, addr;
  //Activate eeprom
  EEPROM.begin(512);

  // Get wifi SSID from eeprom
  addr = eeAddrSSID;
  for (i = 0 ; i < eeSizeSSID ; i++)
  { 
    conf.ssid[i] = EEPROM.read(addr);
    if('\0' == conf.ssid[i])
      break;
    addr++;
  }
  
  // Get wifi PASSWORD from eeprom
  addr = eeAddrPASS;
  for (i = 0 ; i < eeSizePASS ; i++)
  {
    conf.password[i] = EEPROM.read(addr);
    if('\0' == conf.password[i])
      break;
    addr++;
  }
  
  // Get thingspeak api from eeprom
  addr = eeAddrTSAPI;
  for (i = 0 ; i < eeSizeTSAPI ; i++)
  {
    conf.thingspeakApi[i] = EEPROM.read(addr);
    if('\0' == conf.thingspeakApi[i])
      break;
    addr++;
  }
  
  conf.dayStart    = EEPROM.read(eeAddrDayStart);     // Hour of the day that the lamp starts
  conf.dayTime     = EEPROM.read(eeAddrDayTime);      // Number of hours of daylight (LAMP ON)
  conf.pumpFreq   = EEPROM.read(eeAddrPumpFreq);    // Time between 2 pump cycles
  conf.floodedTime = EEPROM.read(eeAddrFloodedTime);  // Time of water at high level

  Serial.println("Application parameters read from eeprom");
  printInfo();
}

/* void serialStack(void)
 *  Stack all serial char received in one string until a '\n'
 *  Set stringComplete to True when '\n' is received
 *  This implementation is good enough for this project because serial commands
 *  will be send slowly.
*/
void serialStack()
{
  if(Serial.available())
  {
    while (Serial.available()) {
      // get the new byte:
      char inChar = (char)Serial.read();
      
      // if the incoming character is a newline, set a flag
      if (inChar == '\n' or inChar == '\r') 
      {// so the main loop can do something about it
        inputString += '\0';
        stringComplete = true;
      }
      else
      {// add it to the inputString
        inputString += inChar;
      }
    }
  }
}

/* void executeCommand(void)
 *  Execute received serial command
 *  Commandes list :
 *   - "i", "I", "info", "Info" : Return basic system informations, mainly for debug purpose
 *   - "FXX"                    : Setup the minimum temperature delta to activate the Airflow (0 to 100 C)
 *   - "fXX"                    : Setup the maximum temperature delta to disable the Airflow (0 to 100 C)
 *   - "S1" or "S0"             : Enable ("S1") or disable ("S0") the summer mode
 *
*/
void executeCommand()
{
  if (stringComplete)
  {
    // Define the appropriate action depending on the first character of the string
    switch (inputString[0]) 
    {
      // INFO Request
      case 'i':
      case 'I':
        printInfo(); // Print on serial the system info
        break;
      // Day start time
      case 'l':
      case 'L':
        inputString.remove(0,1);
        Serial.print("L > change start time of LAMP to : ");
        Serial.println(inputString);
        conf.dayStart = byte(inputString.toInt());
        break;
      // Day duration time
      case 'd':
      case 'D':
        inputString.remove(0,1);
        Serial.print("D > change day time duration of LAMP to : ");
        Serial.println(inputString);
        conf.dayTime = byte(inputString.toInt());
        break;
      // Flooding pump frequency
      case 'R':
      case 'r':
        inputString.remove(0,1);
        Serial.print("P > change pump flooding frequence to : ");
        Serial.println(inputString);
        conf.pumpFreq = byte(inputString.toInt());
        break;
      // Flooding duration
      case 'f':
      case 'F':
        inputString.remove(0,1);
        Serial.print("F > change flooding duration to : ");
        Serial.println(inputString);
        conf.floodedTime = byte(inputString.toInt());
        break;
      // PASSWORD
      case 'P':
      case 'p':
        inputString.remove(0,1);
        Serial.print("P > Change the wifi pasword to : ");
        Serial.println(inputString);
        strcpy (conf.password, inputString.c_str());
        break;
      // SSID
      case 'S':
      case 's':
        inputString.remove(0,1);
        Serial.print("S > Change the SSID to : ");
        Serial.println(inputString);
        strcpy (conf.ssid, inputString.c_str());
        break;
      // Thingspeak channel
      case 'H':
      case 'h':
        inputString.remove(0,1);
        Serial.print("H > Change the thingspeak write api to : ");
        Serial.println(inputString);
        strcpy (conf.thingspeakApi, inputString.c_str());
        break;
      // Write command
      case 'W':
      case 'w':
        inputString.remove(0,1);
        Serial.println("W > Write parameters in eeprom");
        eepromWrite();
        break;
      // Reset default timing config
      case 'T':
      case 't':
        Serial.println("T > Reset timing");
        conf.dayStart    = DAY_START;
        conf.dayTime     = DAY_TIME;
        conf.pumpFreq   = PUMP_FREQ;
        conf.floodedTime = WATER_UP_TIME;
        break;
      // Reset Wifi Credential
      // Reboot
      
      // Ignore (in case of /r/n)
      case '\r':
      case '\n':
      case '\0':
        break;
      
      // Unknown command 
      default: 
        Serial.print(inputString[0]);
        Serial.println(" > ?");
    }
    inputString = "";
    stringComplete = false;
  }
}

/* void executeCommand(void)
 *  Get the board mac address
 * 
 *  Input  : 
 *  Output : result mac address as int for id use
*/
unsigned long getMacAddress() {
  byte mac[6];
  unsigned long intMac = 0;
  unsigned long intMacConv = 0;

  //TODO see for full mac (6 numbers not 4)
  
  WiFi.macAddress(mac);
  for (int i = 0; i < 4; ++i) 
  {
    intMacConv = mac[i];
    intMac = intMac + (intMacConv <<(8*i));
    //Serial.println(String(i));
    //Serial.println(String(intMacConv<<(8*i),HEX));
    //Serial.println(String(intMac, HEX));
  }
  return intMac;
}
