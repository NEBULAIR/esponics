
/*
 *  First implementation of the aaquaponics project on esp8266 with arduino
 *  - Get information from DHT sensor
 *  - Logging on thingspeak
 *  - The use of timers allows to optimize and creat time based environment
 *  - Configuration from serial link
 *  - Configuration saved in eeprom
 *  - Use a table for thingspeak inputs
 *  
 *
 *  Developed from :
 *    https://github.com/atwc/ESP8266-arduino-demos
 *  Use the librairies
 *  - DHT
 *  - Timer in user_interface
 *  
 *  
 *  TODO
 *  - Check for use of hardware interrupt for water sensor, May no be needed
 *  - Add serial config for all parameters
 *  - Add eeprom save of all parameters
 *  - Add thingspeak log on test channel
 *  - Add NTP time read function 
 *  - Add config from internet
 *  
 */
#include "thingspeak_log.h" //temp for test
#include "hardware_def.h"
#include "aquaponics.h"

#include <ESP8266WiFi.h>
#include <DHT.h>

extern "C" {
#include "user_interface.h"
}


#define DEBUG 1 //Use DEBUG to reduce the time 1 minute = 5s

//Serial Config
#define BAUDRATE 115200  // Serial link speed

//Timer elements
os_timer_t myTimer1;
#define TIMER1 1000       //  1s for timer 1
os_timer_t myTimer2;
#define TIMER2 1000*60    // 60s for timer 2
os_timer_t myTimer3;
#define TIMER3 1000*60*5  // 5mn for timer 3
// boolean to activate timer events
bool tick1Occured;
bool tick2Occured;
bool tick3Occured;
// Need to have a global address for the timer id
const char tickId1=1;
const char tickId2=2;
const char tickId3=3;
// Functions declaration
void timerCallback(void *);
void timerInit(os_timer_t *pTimerPointer, uint32_t milliSecondsValue, os_timer_func_t *pFunction, char *pId);

//Wifi config
//Function declarations
void wifiConnect(void);
//Wifi Global variables
WiFiClient client;

//Thingspeak config
String myWriteAPIKey = TS_WRITE_KEY;
//Function declarations
void thingSpeakWrite (String, float, float, float, float, float, float, float, float);

//Sensors config
DHT dht(DHTPIN, DHT22,15);

//Application config
AquaponicsConfig conf;
void ioInits(void);
void printInfo(void);

//Application values
float temperature;
float humidity;
byte waterLevelState = 0;     // water level status memory 

// Time counter variables
volatile int hoursCounter;    // 0 to 23
volatile int secondsCounter;  // 0 to 59
volatile int minutesCounter;  // 0 to 59


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

  ioInits();

  hoursCounter = DAY_START;      // Init the time to DAY_START at startup
  minutesCounter = 1;
  digitalWrite(LAMP, HIGH);

  //Init the config
  strcpy(conf.ssid,WIFI_SSID);
  strcpy(conf.password,WIFI_PASS);
  conf.dayStart = DAY_START;           // Hour of the day that the lamp starts
  conf.dayTime = DAY_TIME;            // Number of hours of daylight (LAMP ON)
  conf.pumpCycle = PUMP_CYCLE;        // Time between 2 pump cycles
  conf.floodedTime = WATER_UP_TIME;   // Time of water at high level

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
    thingSpeakWrite (myWriteAPIKey, temperature, humidity, NAN, NAN, NAN, NAN, NAN, NAN);
  }

  switch (waterLevelState) {
    
    case DOWN:
        //Wait for pump cycle time to activate pump
        if(0 == (minutesCounter % PUMP_CYCLE))
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
        if(0 == ((minutesCounter % PUMP_CYCLE) % WATER_UP_TIME))
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
 
  //Give th time to th os to do his things
  yield();  // or delay(0);
}


/* void timerCallback(void *pArg)
 *  Function 
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
 *  Function 
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
 *  Function 
 *  
 *  Input  : None
 *  Output : None
*/
void wifiConnect(void){
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(conf.ssid);
 
  WiFi.begin(conf.ssid, conf.password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    }
  Serial.println("");
  Serial.println("WiFi connected");
}

/* void thingSpeakWrite(void)
 *  Function 
 *  
 *  Input  : 
 *  Output :
 *  TODO Use a table as input
*/
void thingSpeakWrite (String APIKey,
                      float field1, float field2, float field3, float field4,
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
     client.print("X-THINGSPEAKAPIKEY: "+myWriteAPIKey+"\n");
     client.print("Content-Type: application/x-www-form-urlencoded\n");
     client.print("Content-Length: ");
     client.print(postStr.length());
     client.print("\n\n");
     client.print(postStr);
 
  }
  client.stop();
}

/* void printInfo(void)
 *  Function 
 *  
 *  Input  : 
 *  Output :
*/
void printInfo(void)
{
  
    Serial.print("Time : ");
    Serial.print(hoursCounter);
    Serial.print(":");
    Serial.println(minutesCounter);
    Serial.print("Water level : ");
    Serial.println(waterLevelState);

    Serial.print("Flood every ");
    Serial.print(conf.pumpCycle);
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
 *  Function 
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

