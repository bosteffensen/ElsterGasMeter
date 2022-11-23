/*
  2022-10-14 V0, rewrite af Bo's ESP32_WiFi_Manager gasmåleraflæsning
             class button fjernet, omskrevet til alm. ISR-rutine da kun een instans benyttes
  2022-10-20 V1, websocket opdatering indbygget
             Bestemmelse af tid for aflæsning, ved opslag på NTP-server
             Vis sidst gemte aflæsning ved første opslag på html-siden
             Gemmer Timestamp og counter for sidste aflæsning - så watchdog KAN indarbejdes
             (WTD er ikke indarbejdet i denne version)
  2022-10-21 V2, Watchdog indarbejdet
             Restart og csv-log lagt ud på SD-kort
             ESP32 og SD-kort, se https://microcontrollerslab.com/microsd-card-esp32-arduino-ide/
             Konstateres det, efter opdatering af SD-kortet, at WIFI forbindelsen er tabt kaldes ESP-restart
  2022-10-28 V2_2 Der er /service og /update sub pages, med justering af gascounter, clear af wifi config
             og clear af CSV filen
  2022-10-29 V2_2b lille rewrite
             WriteSD og WriteRestart sammenbygget til WriteSD, med bool parm for append (true -> append)
             Ordinær opdatering af log og restart flyttet til updateSD
             Alle funktionsnavne startes med lille bogstav
             Ny knap på "Forsiden" -> Service
  2022-10-29 V3 Setup, start normalt selv om wifi ikke kan etableres - forudsat at SSID, PSW og IP findes
             i SPIFFS filerne
  2022-11-04 V3_3 Tilføjet m3 pr døgn i sdkort og tilhørende clear af filen på sd.
  2011-11-09 Port for input ændret til GPIO32 - for kompabilitet med OLIMEX ESP32 board - fremtidig brug
  2022-11-11 Tilføjet udskrift af Today : dagens forbrug - ved opstart findes dagens forbrug ved at læse
             sd igennem og tælle antal recordt med dags dato i
  2022-11-01 V4 Indarbejdelse af chart til visning af forbrug i grafisk form.
  2022-11-01 V4_1 Merge af graf i version 3_4 så nu kan der vises graf sammen med det resterende fra 3_4
             En del fejlrettelser i graf/gasreading, og compareTo i stedet for == mellem Strings. 
  2022-11-22 V4_2 Graf-data dannes og gemmes nu kun een gang i døgnet (ved datoskift), så hele log-filen
             ikke skal gennemlæses ved hvert opslag.
             Decimaltegn ændret fra komma til punktum, for at følge angelsk/amerikansk standard for notation.
             Alle SD-kort filnavne ændret fra streng-konstanter i diverse kald, til at benytte de
             definerede konstanter.
  2022-11-22 V4_3 millis i reedSwitch IRQ erstattet af epoch time - fjerner 50 dages wrap araound problematikken
             Farven på 30 dages graf tilføjet.
             Korrekt tilstand af reedswitch ved restart - hvis reed er aktiveret sættes zero og lasttime til aktiveret. LED tændes.
             Fjerner falsk optælling ved genstart og tæller står på nul.
*/

#define DEBUG 1    // turn on debugging set to 1 else turn off set to 0
#if DEBUG == 0
#define debug(x)    Serial.print(x)
#define debugln(x)  Serial.println(x)
#define debugf(x,y) Serial.printf(x,y)
#else
#define debug(x)
#define debugf(x,y)
#define debugln(x)
#endif

#include <Arduino.h>
#include <FunctionalInterrupt.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include <AsyncElegantOTA.h>

// include library for time:
#include "time.h"

// include library for watchdog:
#include <esp_task_wdt.h>

// include library for SD:
#include <FS.h>
#include <SD.h>
#include <SPI.h>
/*GPIOs 34 to 39 are GPIs – input only pins. 
 * These pins don’t have internal pull-up or pull-down resistors. 
 * They can’t be used as outputs, so use these pins only as inputs:
 * /
 */
#define reedPin 32 ///IS ON WROWER NODEmcu 36 is for Olimex

//#define reedPin 36 /// 27 IS ON WROWER NODEmcu 36 is for Olimex
#define IgnoreTime 1UL       // Mindste tid ellem aktivering/deaktivering af reed-relæ, 1 sekund
#define GasVolumePrPulse 10 //  
// SD card Card-Detect pin
//#define SD_Detect         36    // GPIO 36

#define MY_NTP_SERVER "pool.ntp.org"
// Choose your time zone from this list: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define MY_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// Se: https://iotassistant.io/esp32/enable-hardware-watchdog-timer-esp32-arduino-ide/
#define WDT_TIMEOUT 5         // 5 seconds Watchdog timeout

/* ========================================================================================================================= */

/* Data benyttet i ISR, defineres volatile  */
volatile unsigned long  LastIrqTime = 0;
volatile uint32_t       ElsterGasCounter;
volatile bool           reedActivated;
volatile bool           Zero = false;
volatile bool           pressed = false;
volatile static unsigned int currentM3 = 0;
String currentDay = "null";


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";
const char* PARAM_INPUT_5 = "elster";

//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;
String ElsterValue;
String ElsterTimestmp;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";
const char* elsterPath = "/elster.txt";
const char* TimestmpPath = "/timestmp.txt";

// Filepath used on SD-card
const char* logFile        = "/ElsterLog.csv";
const char* restartFile    = "/ElsterRestart.txt";
const char* gasPrDayFile   = "/GasPrDay.csv";
const char* monthDataFile  = "/monthData.txt";


bool wifiActive = false;
IPAddress localIP;
//IPAddress localIP(192, 168, 0, 200);    // hardcoded
IPAddress localGateway;
//IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8,8,8,8);                   // Ang. brug af DNS: Se bemærkning under initTime() !

// Bestemmelse af tid
struct tm timeinfo;

// time used to ignore false IRQ
unsigned long previousMillis = 0;
// Timer variables for Wi-Fi connection, and on-board led (pin 2)
const long interval = 10000;  // Max time to wait for Wi-Fi connection (milliseconds)

// Set LED GPIO
const int ledPin = 2;         // Led on GPIO15

// Til læs/skriv på SD-kort
int     SDpresent;      // Er der indsat et SD-kort i læseren ? (Ja = LOW)
boolean SDstarted = false;
String  ErrStr = "";    // Fejltekst opsættes af SD-kort rutiner
String  dataString;
char    longbuf[100];
/* ========================================================================================================================= */
// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}


/* ========================================================================================================================= */
void IRAM_ATTR reed_ISR() {                   // ISR Interrupt service routine
  debugln("ISR activated");
  if (LastIrqTime + IgnoreTime < getTime()) {  // Hvad sker der når millis tilter ?
    if (!Zero & !digitalRead(reedPin)) {      // Første irq
      LastIrqTime = getTime();                 // Noter tiden for irq
      Zero = true;
      ElsterGasCounter += GasVolumePrPulse;
      digitalWrite(2, HIGH);
      pressed = true;
    }
    else if (digitalRead(reedPin)) {          // Her er vi aktive Zero perioden
      LastIrqTime = getTime();                 // Ignore reload indtil reed slippes igen
      Zero = false;
      digitalWrite(2, LOW);
    }
  }
}

/* ========================================================================================================================= */
//  Læs/Skriv data på SD-kortet
/* ========================================================================================================================= */
boolean startSDcard() {
//  debugln("startSDcard");

  if (!SDstarted) {
    SDstarted = SD.begin();
  }
//  debug("   SD started, result from SD.begin: "); debugln(SDstarted);
  return SDstarted;
}

/* ========================================================================================================================= */
String readRestart(fs::FS &fs, const char *path) { // Restart-filen indeholder kun en record
  debugln("ReadRestart");
  if (!startSDcard()) {
    debugln("   SD NOT started (read restart)");
    return "0001-01-01 00:00:00;0";
  }
  File file = fs.open(path);
  if (!file) {
    debugln("   SD file NOT open (read restart)");
    return "0001-01-01 00:00:00;0";
  }
  String lineBuffer = "";
  while (file.available()) {
    char ch = file.read();
    lineBuffer += ch;
  }
  debugln("   SD file read OK  (read restart)");
  return lineBuffer;
}

/* ========================================================================================================================= */
void writeSD(fs::FS &fs, const char *path, const char *message, bool append) {                // V2_2b
  File file;
  debug("writeSD "); debug(path);
  if (append) debugln(" - Append record to file"); else debugln(" - Create/overwrite file");
  if (ErrStr != "") return;
  /*
    SDpresent = digitalRead(SD_Detect);
    if (SDpresent == HIGH) {
      ErrStr = "00 No SD card";
      return;
    }
  */
  if (!startSDcard()) {
    ErrStr = "01 writeSD: SD not started";
    return;
  }

  if (append) {
    file = fs.open(path, FILE_APPEND);
    if (!file) {
      debugln("   SD NOT open for append (writeSD)");
      File file = fs.open(path, FILE_WRITE);    // Prøv at oprette filen...
      if (!file) {
        ErrStr = "02 writeSD: Create file failed";
        return;
      }
    }
  } else {
    file = fs.open(path, FILE_WRITE);
    if (!file) {
      ErrStr = "03 writeSD: Create/Overwrite file failed";
      return;
    }
  }    
  if (file) {
    file.print(message);
    file.print("\r\n");
    file.close();
    debugln("writeSD: File written OK");
  }
}

/* ========================================================================================================================= */
void updateSD() {                                                                               // V2_2b
  ElsterTimestmp = formatTimestamp();
  String dataString = ElsterTimestmp + ";" + (String)ElsterGasCounter;
  Gas_m3_PrDay(ElsterTimestmp);
  dataString.toCharArray(longbuf, 100);
  writeSD(SD, restartFile, longbuf, false);           // Overwrite file, so it only holds the last record
  if (ErrStr != "") debugln(ErrStr);
  writeSD(SD, logFile, longbuf, true);                // Append new rcord to file, giving a full log
  if (ErrStr != "") debugln(ErrStr);
  debugln("New record written: " + dataString);
}

/* ========================================================================================================================= */
// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    debugln("An error has occurred while mounting SPIFFS");
  }
  debugln("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path) {
  debugf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    debugln("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message) {
  debugf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    debugln("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    debugln("- file written");
  } else {
    debugln("- write failed");
  }
}

/* ========================================================================================================================= */
// Initialize WiFi
bool initWiFi() {

  WiFi.mode(WIFI_STA);
  localIP.fromString(ip.c_str());
  localGateway.fromString(gateway.c_str());

  // Ang. brug af DNS: Se bemærkning under initTime() !
  if (!WiFi.config(localIP, localGateway, subnet, dns)) {
    debugln("STA Failed to configure");
    return false;
  }
  WiFi.begin(ssid.c_str(), pass.c_str());
  debugln("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      debugln("Failed to connect.");
      return false;
    }
  }

  debugln(WiFi.localIP());
  return true;
}

/* ========================================================================================================================= */
// Tilrettet klip fra https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/
void initTime(String timezone) {

  debugln("Setting up time");
  // OBS: Funktionen getLocalTime() kan fejle når der benyttes fast IP-adresse:
  // se: https://www.esp32.com/viewtopic.php?t=10507
  configTime(0, 0, MY_NTP_SERVER);    // First connect to NTP server, with 0 TZ offset
  if (!getLocalTime(&timeinfo)) {
    debugln("  Failed to obtain time");
    return;
  }
  debugln("  Got the time from NTP");
  // Set the real timezone
  debugf("  Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1); //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

String formatTimestamp() {
  char   timeStringBuff[20];

  if (!getLocalTime(&timeinfo)) {
    debugln("Failed to obtain time");
    return "0001-01-01 00:00:00";
  }

  strftime(timeStringBuff, sizeof(timeStringBuff), "%F %T", &timeinfo);
  return timeStringBuff;
}

/* ========================================================================================================================= */
// Replaces placeholder with GasCounter
void notifyClients() {
  String GAS = (String)ElsterGasCounter;

//  ws.textAll(ElsterTimestmp + " " + GAS.substring(0, (GAS.length() - 3)) + "," + GAS.substring(GAS.length() - 3) + " m3 Today: " + currentM3);
  ws.textAll(GasUsrStr(ElsterTimestmp,ElsterGasCounter,currentM3));
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {                                   // V2_2
  debugln("WS message");
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "Clear") == 0) {
      writeFile(SPIFFS, ssidPath, ""); //clear ssid 
      ESP.restart();
    }

    if (strcmp((char*)data, "ClrCSV") == 0) {
      String dataString = formatTimestamp() + ";" + (String)ElsterGasCounter;
      dataString.toCharArray(longbuf, 100);
      writeSD(SD, logFile, longbuf, false);       // Overwrite logfile, so it only holds the last record
      if (ErrStr != "") debugln(ErrStr);
    }

    if (strcmp((char*)data, "ClrCSVDay") == 0) {
      String dataString = formatTimestamp() + ";0" ;
      dataString.toCharArray(longbuf, 100);
      writeSD(SD, gasPrDayFile, longbuf, false);  // Overwrite file
      if (ErrStr != "") debugln(ErrStr);
    }


    if (strcmp((char*)data, "-") == 0) {
      ElsterGasCounter -= GasVolumePrPulse;
      updateSD();
      notifyClients();
    }

    if (strcmp((char*)data, "+") == 0) {
      ElsterGasCounter +=GasVolumePrPulse;
      updateSD();
      notifyClients();
    }
        String GasChange = (char*)data;

    if (GasChange.toInt() > 0){
      
      debug("SetCounter to : ");
      debugln((char*)data);
      ElsterGasCounter = GasChange.toInt();
      debugln(ElsterGasCounter);
      updateSD();
      notifyClients();
      
    }

  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
    //  ("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      debugf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var) {
  debugln("Processor parm: " + var);
//  String GAS = (String)ElsterGasCounter;
  if (var == "STATE") {   // Startværdi der vises når siden åbnes
  //  return ElsterTimestmp + " " + GAS.substring(0, (GAS.length() - 3)) + "," + GAS.substring(GAS.length() - 3) + " m3 Today: " + currentM3;
  return GasUsrStr(ElsterTimestmp,ElsterGasCounter,currentM3);
  }
}

/* ========================================================================================================================= */
/* =================================================================================                              #SETUP     */
void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  // Set GPIO 2 as an OUTPUT
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  initSPIFFS();
  // Load values saved in SPIFFS, if any
  ssid            = readFile(SPIFFS, ssidPath);
  pass            = readFile(SPIFFS, passPath);
  ip              = readFile(SPIFFS, ipPath);
  gateway         = readFile(SPIFFS, gatewayPath);
  
  debugln(ssid);
  if (pass != "") debugln("PSWD found"); else debugln("PSWD not found !");
  debugln(ip);
  debugln(gateway);

  String restart = readRestart(SD, restartFile);
  ElsterTimestmp   = restart.substring(0, 19);
  ElsterValue      = restart.substring(20);
  ElsterGasCounter = ElsterValue.toInt();
  debugln(ElsterTimestmp);
  debugln(ElsterValue);
  if (ssid != "" && pass != "" && ip != "") {
    debugln("SSID, PSW and IP address found, normal startup");

    wifiActive = initWiFi();    // Connect to Wi-Fi network with SSID and password

    if (wifiActive) {
      initTime(MY_TZ);
      debug("Start: "); debugln(formatTimestamp());
      // init of currentM3 demands date 
      currentM3 = initCurrentM3(logFile,formatTimestamp()); //  ReCalculate this days counts - if any set to countet value else keep "null"
      if (currentM3)
      {
        currentDay = formatTimestamp().substring(0,10); //.substring(0, 10);
        //currentDay = currentDay.substring(0,10);
      }
      // Print ESP Local IP Address
      initWebSocket();
      // Route for root / web page
      server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(SPIFFS, "/index.html", "text/html", false, processor);
      });
      
      server.on("/service", HTTP_GET, [](AsyncWebServerRequest * request) {       // V2_2
        request->send(SPIFFS, "/service.html", "text/html", false, processor);
      });
  
      // Entry for sending CSVfile back...
      server.on("/GasData", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(SD, logFile, "text/plain");
        debugln("CVSsend");
      });
       // Entry for sending CSVfile back...
      server.on("/GasPrDay", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(SD, gasPrDayFile, "text/plain");
        debugln("CVSsend2");
      });
      // Entry for sending data to bar chart
      server.on("/readings", HTTP_GET, [](AsyncWebServerRequest * request) {
        request->send(200, "text/plain", getGasReadings());
        Serial.println("Data send");
      });
 
      server.serveStatic("/", SPIFFS, "/");
      AsyncElegantOTA.begin(&server);             // Start ElegantOTA
      // Start server
      server.begin();

      buildGasReadings(formatTimestamp());       // Dan data til bar-graf
      
//  Serial.print("Initializing SD card...");
//
//  if (!SD.begin()) {
//    Serial.println("initialization failed!");
//    while (1);
//  }
//  Serial.println("initialization done.");
//
//      Serial.println("Latest date from SD: " + getLatestDateSD("/ElsterLog.csv")); //fjernes...
    } else {
      
      // NO wifi available, time is 0001-01-01 00:00:00
      String timeStampFrmSD = getLatestDateSD(logFile); //read through sd oan get latest date
      debug("Latest timestamp from SD: " + timeStampFrmSD );
      strptime(timeStampFrmSD.c_str(),"%Y-%m-%dT%H:%M:%S.%03dZ",&timeinfo);
      time_t epoch_ts = mktime(&timeinfo);
      struct timeval tv_ts = {.tv_sec = epoch_ts};
      settimeofday(&tv_ts, NULL);
      debug("No WIFI - Time is set to: "); debugln(formatTimestamp()); //Default time setting 
    }
  
    // Aktiver ISR (Reed rælæ slutter til GND)
    pinMode(reedPin, INPUT_PULLUP);
    // test for init tilstand af reed - hvi den er stoppet på nul sættes port til denne tilstand
    if (!Zero & !digitalRead(reedPin)) {      // Første irq
      LastIrqTime = getTime();                 // Noter tiden for irq
      Zero = true;
      digitalWrite(2, HIGH);
    }

    
    attachInterrupt(reedPin, reed_ISR, CHANGE);

    // Configure Watchdog
    esp_task_wdt_init(WDT_TIMEOUT, true);   //enable panic so ESP32 restarts
    esp_task_wdt_add(NULL);                 //add current thread to WDT watch
    
  } else {    // Hvis SSID, PSW eller IP ikke findes i SPIFFS - opret et åbent softAP

    debugln("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("ElsterBK-G4T", NULL);

    IPAddress IP = WiFi.softAPIP();
    debug("AP IP address: ");
    debugln(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });

    server.serveStatic("/", SPIFFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            debug("SSID set to: ");
            debugln(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            debug("Password set to: ");
            debugln(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            debug("IP Address set to: ");
            debugln(ip);
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
            debug("Gateway set to: ");
            debugln(gateway);
            // Write file to save value
            writeFile(SPIFFS, gatewayPath, gateway.c_str());
          }
          // HTTP POST Elster GasMeter value
          if (p->name() == PARAM_INPUT_5) {
            //            ElsterValue= p->value().c_str();
            ElsterValue = p->value();
            debug("GasMeter set to: ");
            debugln(ElsterValue);
            // Write file to save value
            // writeFile(SPIFFS, elsterPath, ElsterValue.c_str());
            String dataString = "0001-01-01 00:00:00;" + ElsterValue;
            dataString.toCharArray(longbuf, 100);
            writeSD(SD, restartFile, longbuf, false);
          }
        }
      }
      request->send(SPIFFS, "/wifimanagerEnd.html", "text/html");
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }

}

/* ========================================================================================================================= */
/* =================================================================================                                   #LOOP */
void loop() {
  static unsigned long OldGasReading = 0;

  esp_task_wdt_reset();

  ws.cleanupClients();
  if (pressed) {
    updateSD();
    pressed = false;
    debug("WiFi.status: "); debugln(WiFi.status());
    if (WiFi.status() != WL_CONNECTED){ 
      debugln("Wifi reconnect...");
      WiFi.disconnect();
      debugln("Wifi is now disconnected");
      WiFi.reconnect();
      delay(2000);
      if (WiFi.status() == WL_CONNECTED){ 
        debugln("Wifi reconnected OK");
        wifiActive = true;
      } else {
        debugln("Wifi not reconnected");
      }
    }
  }

  if (OldGasReading != ElsterGasCounter) {
    OldGasReading = ElsterGasCounter;
    notifyClients();  // Send ny gasCounter
  }
}
