/***************************************************
Author: Luke Harrison
Project: 3D Printing - Simple, cheap dehumidifier
Project summary: 
Designed to be a solution to 3D printing filament absorbing humidity over time.
Food dehumidifiers were unreasonably priced so I decided to use an old Sonoff Basic
that I had laying around and hacked some home automation code into a dehumidifier.

Use this code under the following conditions:
1.) Never comment about how hacky it is. It works and that is all I was aiming for.
2.) Pull requests are welcome if you would like to make it less hacky.

Basic operation is:
1.) Init all peripherals including FS, IO, DHT, WiFi, etc...
2.) Enter into loop where temp and humidity are read once every 4s.
3.) If there are 6 successive bad (nan) reads then the unit reboots itself as a safety measure.
4.) If it has been turned on via the webserver then it will pulse the relay on and off
    according ensuring that the temperature remains between the bounds for the type of filament selected.
5.) If the relay is being driven and the temperature does not change or decreases over a 2 minute period
    then it is assumed that the sensor is faulty and the unit shuts down as a safety measure.
****************************************************/


/***************************************************************************/
/***************************************************************************/
// Required libraries

#include <ESP8266WiFi.h>  //WiFi communication libraries.
#include <EEPROM.h>       //non-volatile storage of switch state.
#include <Ticker.h>       //timer
#include <ESP8266mDNS.h>  //multicast DNS library used for resolution of remote module IP addresses.
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>           //SPIFFS is prefered over EEPROM because of wear leveling.

#include <Arduino.h>

#include "DHT.h"       //DHT 22 ESP library.

extern "C" {
#include <user_interface.h>     //Gives us access to the boot type.
}
/***************************************************************************/


/***************************************************************************/
/***************************************************************************/
// #Defines
#define DEBUGs
#define DEBUG

#define inverted 0        //Set this to 1 for an inverted output system and to 0 for a normal system.


#define CONNECTION_RESET_TIME 300 //measured in seconds (5 minutes)
#define ON 0xAC
#define OFF 0xCA
#define DEVICENAME "dehumidifier"  //replace the text in inverted commas with whatever the name of your device is...

#ifdef DEBUGs
  #define DEBUGSSTRLN(x) Serial.println(x);
  #define DEBUGSSTR(x) Serial.print(x);
  #define DEBUGSTRLN(x) Serial.println(x);
  #define DEBUGSTR(x) Serial.print(x); 
#else  
  #define DEBUGSSTRLN //
  #define DEBUGSTRLN //
  #define DEBUGSTR //
  #define DEBUGSTRLN //
  #define DEBUGSSTR //
#endif


//Defines for dehumidifier functions
#define ERRORLIMIT 6      //This will result in 12s before the device shuts itself down.
#define MINTEMP_PLA 40        //For standard PLA
#define MAXTEMP_PLA 46        //For standard PLA. Glass transition temp is 60C so we are well below the safe limit but this will take a lot longer to dry. In the region of 10+ hours.
#define MAXTEMP_PETG 76      //For PETG. Glass transition temp is 88C. Drying time will be around 5 hours.
#define MINTEMP_PETG 70      //For PETG
#define HISTORYPOINTS 30   //The number of points that will be stored in the history buffer to analyse if it is responding according to the drive signal.
#define DHTTYPE DHT22   // DHT 22  (AM2302)
#define DHTPIN 14
/***************************************************************************/


/***************************************************************************/
/***************************************************************************/
//Variable and object definitions.

Ticker tickerOSWatch;   //this object will watch the WiFi to make sure that it is connected. If not, it will restart the module after some time assuming a low level library or hardware malfunction.
Ticker tickeripWatch;  //this object will provide a timer that allows us to poll the input pin and monitor for stable changes.

// WiFi parameters. Enter your SSID and password here.
const char* ssid = "ENTER_YOUR_WIFI_SSID_HERE";
const char* password = "ENTER_YOUR_WIFI_PASSWORD_HERE";


//OTA string constants
const char* host = DEVICENAME;
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";

//SPIFFS constants
const char* state_path = "/config/on.txt";
const char* filament_path_pla = "/config/pla.txt";    //To save time I will just write a file with the name of the filament type. This way I don't have to worry about reading and parsing etc...PLA is default.
const char* filament_path_petg = "/config/petg.txt";    //To save time I will just write a file with the name of the filament type. This way I don't have to worry about reading and parsing etc...PLA is default.


ESP8266WebServer httpServer(80);
ESP8266WebServer lightserver(8080);         //This is the server that will handle the HTTP requests to turn the light on/off and query the state.
ESP8266HTTPUpdateServer httpUpdater;

static unsigned long last_connect;  //This contains the runtime marker (in ms) from the last point that the module was connected to WiFi.

int output_pin = 12;   //Define the GPIO which will be used to drive the relay.
int input_pin = 0;     //This is the button on the Sonoff basic
int led_pin = 13;      //This pin will drive the LED to show that the code is booting.
int dht_pin = 14;      //This pin is the final one on the programming header and easy to access.

bool switch_change_detected=0;  //use this to flag any changes to the input switch.

//EEPROM
int address = 0;      //first address in EEPROM
byte value = 0;       //initialise the value within the EEPROM read holder.

//DHT variables
DHT dht(DHTPIN, DHTTYPE);
//DHTesp dht;             //This object defines the object that will be used to configure and access the DHT sensor.
int sample_timer = 0;   //This is a timer that will be used to count the time between samples of the DHT.
float humidity = 100;   //These are set to 'safe' values which will cause the control loop to turn off the relay on reboot if no samples can be obtained.   
float temperature = 55; //Same as above.
int dht_errorcount = 0; //Used to measure the number of acceptable, successive errors.
int sensorfail = 0;     //Used to monitor whether the sensor is responding proportionate to the drive signal.
float temperaturehist[HISTORYPOINTS]; //This will offer HISTORYPOINTSx2s of temperature history.
int tempcount = 0;      //This counts how many temperature sample points we have taken since the last buffer flush.
int dehumidifierstate = 0;  //We will initialise it to off.
float MINTEMP = 0;      //Safe init.
float MAXTEMP = 1;      //Safe init.  Note that we will need to specify the filament type when turning the unit on in order to get anything out of it.
enum filament_type{pla = 0, petg = 1};
enum filament_type filamenttype=pla;


/***************************************************************************/
/***************************************************************************/
/***************************************************************************/
//Function definitions.

//This function will be called periodically by the OS. If it detects that the WiFi has not been connected for more than 5 minutes it resets the module.
void ICACHE_RAM_ATTR osWatch(void) {
    unsigned long t = millis();   //current OS time.
    unsigned long last_abs_connect = abs(t - last_connect);
    DEBUGSTRLN("CONNECTION TIMER CHECK"); //This line may mess with the WiFi connection.
    //Toggle the heartbeat LED.
    //digitalWrite(led_pin,!digitalRead(led_pin));
    
    if ((WiFi.status() != WL_CONNECTED) && (last_abs_connect >= (CONNECTION_RESET_TIME * 1000)))  //Bugfix: Added the check for WiFi status here to ensure that an overflow of t does not result in an erroneous reset.
    {
      DEBUGSSTR("WIFI NOT CONNECTED. TIME TO RESET: ");
      DEBUGSSTR(last_abs_connect);
      DEBUGSSTRLN("ms");
      // save the hit here to eeprom or to rtc memory if needed
      DEBUGSSTRLN("RESETTING BECAUSE THE WIFI WAS NOT CONNECTED FOR 5 MINUTES");
      ESP.restart();  // normal reboot 
      //ESP.reset();  // hard reset
    }
}

/***************************************************************************/
//This function will be called every 50ms. It will debounce any changes to the input and it will also count the time needed between samples of the DHT.
void ICACHE_RAM_ATTR ipWatch(void) {
  static byte last_known_ip;  //this contains the last known state of the input
  static byte debounce_tmr = 0; //initialise to zero for the first iteration of this function.
  byte debounce_ip = 0;       //this is a temporary variable used during a single iteration of the debounce process.
  File f;                 //result container for SPIFFS queries
  byte delres = 0;
 // DEBUGSTRLN(digitalRead(input_pin));

  //First let's decrement the DHT sample timer so that the main loop knows when to take a sample.
  if (sample_timer) sample_timer--;       //This should ensure that we never roll below zero.
   
  if ((last_known_ip != ON) && (last_known_ip != OFF)) //this is the first time that the ticker is running and it has no valid value.
  {
    if (digitalRead(input_pin)) 
    {
      last_known_ip = ON; //the pin is currently high
    } else 
    {
      last_known_ip = OFF;  //the pin is currently low
    }
  }

  //update the debounce input.
  if (digitalRead(input_pin)) 
  {
    debounce_ip = ON; //the pin is currently high
  } else 
  {
    debounce_ip = OFF;  //the pin is currently low
  }

  
  //we need to check if there has been a change since the last check and if so then start the debounce process.
  if ((last_known_ip != debounce_ip) && (!debounce_tmr)) //there is a mismatch between the last known and the debounce value and we are not in a debounce cycle.
  {
    debounce_tmr = 3; //150ms until we make the change.
    return; //there is nothing left to do in the loop since we do not want to move on to the debounce routine as the timer has just been set.
  } else if(last_known_ip == debounce_ip)
  {
    debounce_tmr = 0; //during the loop the pin went back to the lask known value so kill the debounce procedure.
  }

  if (debounce_tmr)   //the debounce routine is active
  {
    if (!--debounce_tmr)  //the timer has expired
    {
      last_known_ip = debounce_ip;  //set the last known input to the debounced input.
      //digitalWrite(output_pin,!digitalRead(output_pin));  //This is only required if we are going to use a switch. Putting this here is not best practice but it does allow for rapid respose.
      if (digitalRead(output_pin))    //Leaving this here will simply update the state of the output so no harm. The input pin is 
      {
        f = SPIFFS.open(state_path, "w+");
        DEBUGSTRLN("Just wrote the SPIFFS on and the result was: "); 
        DEBUGSTRLN(f);
        f.close();    //We don't just want to leave the file open. Time to close it.
      } else
      {
        delres = SPIFFS.remove(state_path);
        DEBUGSTRLN("Just wrote the SPIFFS off and the result was: "); 
        DEBUGSTRLN(f);
      }
      DEBUGSTRLN("INPUT CHANGE DETECTED");
    }
  }
}

/***************************************************************************/
//this function contains all of the timer setup code.
void Timersetup(void){
  tickerOSWatch.attach_ms(10000, osWatch);  // Call the connection checker every 10s
  tickeripWatch.attach_ms(50, ipWatch);     // Call the debounce function once every 50ms
}

/***************************************************************************/
//this function handles the setup of all GPIO
void GPIOsetup(void){

  pinMode(output_pin, OUTPUT);            // Prepare GPIO2 which is the relay output.
  pinMode(input_pin, INPUT_PULLUP);       // Not used in this sketch but included because maybe...
  pinMode(led_pin, OUTPUT);               // Just used to indicate boot status in this sketch.
  digitalWrite(output_pin,0);             // Initialise the relay output so that it is off at the start.
}



/***************************************************************************/
//this function will initialise the SPIFS for reading and writing
void SPIFFSsetup(void){
  rst_info *resetInfo;    //Structure that contains the reset type.
  File f;                 //Result container for SPIFFS queries
  SPIFFS.begin();         //Init the SPIFS

  //for any reset other than a power on reset the last known file state will be used to determine whether or not to turn on. This is still acceptable for a failsafe system.
  if (SPIFFS.exists(state_path)){  
    dehumidifierstate = 1;          //Let the control algorithm know that it is good to go.
  } else {
    dehumidifierstate = 0;          
  }
  
}


/***************************************************************************/
//this function will initialise the serial port only if debug mode is enabled.
void Serialsetup(void){
  rst_info *resetInfo;    //Structure that contains the reset type.
  resetInfo = ESP.getResetInfoPtr();
  // Start Serial
  #ifdef DEBUG
  Serial.begin(115200,SERIAL_8N1,SERIAL_TX_ONLY);
  //Serial.begin(115200);   // Initialising the serial in this manner allows us to use RX as an input
  delay(1000);              // This delay allows time for the port to init before we push something down the pipe.
  DEBUGSTR("Reset reason: ");
  DEBUGSTRLN(resetInfo->reason); 
  #endif

}

/***************************************************************************/
//The setup function for the WiFi
void WiFisetup(void){
  // Connect to WiFi network
  DEBUGSSTRLN("");
  DEBUGSSTR("Connecting to ");
  DEBUGSTRLN(ssid);
 
  WiFi.mode(WIFI_STA);  //set the WiFi mode to be a device on the network and not an AP
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUGSSTR(".");
  }
  
  last_connect = millis();    //mark the last time we were connected to the network.
  DEBUGSTRLN("");
  DEBUGSTRLN("WiFi connected");
}


/***************************************************************************/
//Since this code came from a home automation implementation it still uses a lot of terminology associated with that use case.
//This function handles http requests to turn the dehumidifier on/off.
void handle_light(){
  File result; //This holds the SPIFFS read/write result.
  byte deleteresult = 0;
  
  // get the value of request argument "state" and convert it to an int
  int lightstate = lightserver.arg("state").toInt();

  lightserver.send(200, "text/plain", String("De-humidifier is now ") + ((lightstate)?"on":"off"));
  //Let's also write the result to the flash...
  if (lightstate){
    result = SPIFFS.open(state_path, "w+");
    result.close();     //Since we don't want to do anything to the file other than create it we can close it immediately. 
    dehumidifierstate = 1; 
  } else {
    deleteresult = SPIFFS.remove(state_path);
    dehumidifierstate = 0;
  }
}
/***************************************************************************/

/***************************************************************************/
//This function handles http requests for the state of the dehumidifier.
void handle_state(){
  char textarray[110];
  
  //Check if the file exists...
  if (SPIFFS.exists(state_path)){ 
    (void)sprintf(textarray,"STATE = ON, RELAY STATE = %d, TEMP = %f, HUMIDITY = %f, SENSORFAIL = %d, FILAMENT TYPE = %s", digitalRead(output_pin), temperature, humidity, sensorfail, ((filamenttype)?"PETG":"PLA"));
    lightserver.send(200, "text/plain", textarray);      //TODO include temp and humidity results in this output as well as the current relay state.
  } else {
    (void)sprintf(textarray,"STATE = OFF, RELAY STATE = %d, TEMP = %f, HUMIDITY = %f, SENSORFAIL = %d, FILAMENT TYPE = %s", digitalRead(output_pin), temperature, humidity, sensorfail, ((filamenttype)?"PETG":"PLA"));
    lightserver.send(200, "text/plain", textarray);
  } 
}
/***************************************************************************/

/***************************************************************************/
//This will set the filament type in flash using a filename. It's nasty in that it literally just creates a file with the name of the filament type and manually erases all others but it works.
void handle_filament(){
  File result; //This holds the SPIFFS read/write result.
  byte deleteresult = 0;

  DEBUGSTRLN("HANDLING FILAMENT");
  // get the value of request argument "state" and convert it to an int
  filamenttype = (filament_type)lightserver.arg("type").toInt();

  
  if (filamenttype != pla && filamenttype != petg){   //Invalid type requested.
    lightserver.send(200, "text/plain", String("Invalid filament type requested"));
    DEBUGSTRLN("FOUND AN INVALID TYPE");
  } else {
    lightserver.send(200, "text/plain", String("Filament type now set to ") + ((filamenttype)?"PETG":"PLA"));
    //Let's also write the result to the flash...
    if (filamenttype == petg){
      DEBUGSTRLN("FOUND PETG");
      deleteresult = SPIFFS.remove(filament_path_pla);      //Get rid of the pla file.
      result = SPIFFS.open(filament_path_petg, "w+");
      result.close();     //Since we don't want to do anything to the file other than create it we can close it immediately. 
      MINTEMP = MINTEMP_PETG;
      MAXTEMP = MAXTEMP_PETG;
    } else if (filamenttype == pla) {
      DEBUGSTRLN("FOUND PLA");
      deleteresult = SPIFFS.remove(filament_path_petg);      //Get rid of the pla file.
      result = SPIFFS.open(filament_path_pla, "w+");
      result.close();     //Since we don't want to do anything to the file other than create it we can close it immediately.
      MINTEMP = MINTEMP_PLA;
      MAXTEMP = MAXTEMP_PLA;
    }
  }
}
/***************************************************************************/

/***************************************************************************/
//The setup function for the light server
void TCPServersetup(void){
  // Start the server
  //server.begin();
  lightserver.on("/light", handle_light);     //This will invoke the handle_light function when the /light URI is invoked.
  lightserver.on("/state", handle_state);     //This will invoke the handle_state function when the /state URI is invoked.
  lightserver.on("/filament", handle_filament);     //This will invoke the handle_filament function when the /filament URI is invoked. Use /filament?type=pla or petg to set it to a given type.

  //With the handlers defined for the server we can now start it.
  lightserver.begin();
  DEBUGSTRLN("Server started");
  // Print the IP address
  DEBUGSTRLN(WiFi.localIP());
}

/***************************************************************************/
//The setup function for the mDNS
void mDNSsetup(void){
 if (!MDNS.begin(host)) {
    DEBUGSTRLN("Error setting up MDNS responder!");
    while(1) { 
      delay(1000);
    }
  }
  DEBUGSTRLN("mDNS responder started");
  
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin(); 
  
  MDNS.addService("http", "tcp", 80);       //add the OTA updater service.
  //Serial.println("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n");
}
/***************************************************************************/

//This is the setup function for the DHT sensor.
void DHTsetup(void){
  byte deleteresult = 0;
  File result;
  
  dht.begin();
  sample_timer = 2000*2/50;  //The counter is now initialised so we will wait this long before we take our first sample.
  memset(temperaturehist,0,sizeof(temperature)*HISTORYPOINTS);
  DEBUGSTR("Size of temperature array is ");
  DEBUGSTRLN(sizeof(temperature)*HISTORYPOINTS);
  
  //Finally let's see what filament type we have been configured to use...
  if (SPIFFS.exists(filament_path_pla) && SPIFFS.exists(filament_path_petg)){  
    //Impossible error state.
    DEBUGSTRLN("The code has reached an impossible error state. The limits were always in our heads. Reach for your dreams.");
    DEBUGSTRLN("Erasing all filament type files and resetting to PLA.");
    deleteresult = SPIFFS.remove(filament_path_petg);
    deleteresult = SPIFFS.remove(filament_path_pla);
    result = SPIFFS.open(filament_path_pla, "w+");
    result.close();
    MAXTEMP = MAXTEMP_PLA;
    MINTEMP = MINTEMP_PLA;
    filamenttype = pla;
  } else if(SPIFFS.exists(filament_path_pla)) {
    MAXTEMP = MAXTEMP_PLA;
    MINTEMP = MINTEMP_PLA;
    filamenttype = pla;        
  } else if(SPIFFS.exists(filament_path_petg)){
    MAXTEMP = MAXTEMP_PETG;
    MINTEMP = MINTEMP_PETG;
    filamenttype = petg;       
  } else {
    //It must be the first boot because nothing exists...
    MAXTEMP = MAXTEMP_PLA;
    MINTEMP = MINTEMP_PLA;
    filamenttype = pla;
    result = SPIFFS.open(filament_path_pla, "w+");    
    result.close();   
  }
}

/***************************************************************************/
//The setup function which performs init etc...
void setup() {

  Timersetup();

  GPIOsetup();

  digitalWrite(led_pin,0);    //Show that we are booting.

  SPIFFSsetup();

  Serialsetup();
 
  WiFisetup();

  TCPServersetup();

  mDNSsetup();

  DHTsetup();                 //Setup function for the DHT sensor.

  digitalWrite(led_pin,1);    //Show that we are done booting.
  
}
/***************************************************************************/



/***************************************************************************/
void loop() {
File result; //This holds the SPIFFS read/write result.
byte deleteresult = 0;
char debugtext[90];

   //Check that we are still connected to the WiFi network
  if (WiFi.status() == WL_CONNECTED){  
    last_connect = millis();    //reset the connection timer.
  }

  
  //check if there is an OTA update available
  httpServer.handleClient();
  
  //Check if there has been any communication with the light server.
  lightserver.handleClient();
  
  //Check if it is time to take a sample
  if ((sample_timer <= 0) || (sample_timer > 2000*2/50)){
    sample_timer = 2000*2/50;
    
    //Read the humidity.
    humidity = dht.readHumidity();
    (void)sprintf(debugtext,"HUMIDITY READ AS %f",humidity);
    DEBUGSTRLN(debugtext);
    if (isnan(humidity)){
      dht_errorcount++;                 //increment the number of measured errors.
    } else{
      dht_errorcount = 0;               //zero the number of measured errors.
    }

    //Read the temp
    temperature = dht.readTemperature();
    (void)sprintf(debugtext,"TEMP READ AS %f",temperature);
    DEBUGSTRLN(debugtext);
    if (isnan(temperature)){
      dht_errorcount++;                 //increment the number of measured errors.
      (void)sprintf(debugtext,"ERROR READING TEMP NUMBER %d",dht_errorcount);
      DEBUGSTRLN(debugtext);
    } else{
      dht_errorcount = 0;               //zero the number of measured errors.
      if (!sensorfail && (dehumidifierstate == 1)){                 //if we have already determined that the sensor has failed then we don't want to continue. We also don't want to consider the history if the dehumidifier algorithm is off.
        temperaturehist[tempcount] = temperature;
        tempcount++;                      //Increment the number of measured temperature points.
        if (tempcount >= HISTORYPOINTS){  //If we have enough points to run a difference...
          //Here we need to check whether the change is proportional to the drive signal 
          float tempdiff = temperaturehist[HISTORYPOINTS-1]-temperaturehist[0];   //Difference between the last and first sample.          
          //if (tempdiff == 0) sensorfail = 1;    //We are measuring to a single decimal point. There must be a difference.
          //if ((tempdiff > 0) && !digitalRead(output_pin)) sensorfail = 1;   //The temperature has increased but the relay was off. Since the container has great insulation this part is prone to failure. Since the sensor is already off this check is really void.
          if ((tempdiff <= 0) && digitalRead(output_pin)) sensorfail = 1;    //The temperature has decreased or stayed the same but we were driving the relay.
          tempcount = 0;                                                    //Reset the counter to 0
           (void)sprintf(debugtext,"TEMPDIFF READ AS %f with RELAY OUTPUT AS %d. SENSORFAIL IS %d",tempdiff,digitalRead(output_pin),sensorfail);
          DEBUGSTRLN(debugtext);         
          memset(temperaturehist,0,sizeof(temperature)*HISTORYPOINTS);      //Reset the history array to 0
        }
      }
    }

    //Since we are in a sample loop let's check if we have had too many errors and if so then we would want to reset the device. No games.
    if (dht_errorcount >= ERRORLIMIT){
      delay(2000);      //This should give the ESP time to flush the UART TX buffer and send all of the latest debug messages.
      ESP.restart();    //This will leave the system in whatever state it was in before the restart. That being said, the control loop will never turn anything on since the variables are initialized over the high limits.
    }

    //Here we want to check if the temperature sensor is responding according to the output. If the output is high but the sensor is stagnant or decreasing then we have a problem.
  
  }

  //We only run the control loop if we are meant to be on.
  if (dehumidifierstate == 1){
    //Control loop. This will run ever iteration of the main loop but since the samples are only coming ever 2s it will not take any action at a higher frequency.
    //The loop will only adjust the relay when a limit is reached. In this way the hysteresis band obeys the last given command.
    if ((temperature <= MINTEMP) && !sensorfail){
      //First we want to check if we are at a turn around point.
      if (!digitalRead(output_pin)){
        //Flush the buffer that holds the last temperature values.
        memset(temperaturehist,0,sizeof(temperature)*HISTORYPOINTS);
        tempcount = 0;
      }
      digitalWrite(output_pin,1);                   //We are below the minimum threshold so let's drive the heating element on.
    }
  
    if (temperature >= MAXTEMP || sensorfail){
      //Check if this is a turnaround...
      if (digitalRead(output_pin)){   //The output was driving the relay but it should now be off
        //Flush the buffer that holds the last temperature values.
        memset(temperaturehist,0,sizeof(temperature)*HISTORYPOINTS);
        tempcount = 0;
      }
      digitalWrite(output_pin,0);                   //We are above the minimum threshold so let's drive the heating element off.
    }
  } else {
    digitalWrite(output_pin,0);
  }

}
/***************************************************************************/
