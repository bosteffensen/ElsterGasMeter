/* ========================================================================================================================= */
// Get the last 30 days of Gas Readings and return data
// https://randomnerdtutorials.com/esp32-web-server-gauges/
// https://randomnerdtutorials.com/esp32-esp8266-plot-chart-web-server/

String getGasReadings() {
  debugln("*** getGasReading() ***");
  File file;
  String dataStr;
  // open file for reading
  file = SD.open(monthDataFile, FILE_READ);
  if (file) {
    while (file.available()) {
      dataStr = file.readStringUntil('\n');
    }
    file.close();
  } else {
    debug(F("SD Card: error on opening file"));
    dataStr =  "{\"0001-01-01\":\"Gas\":0.0}";
  }
  debugln(dataStr);
  return dataStr;

}

String buildGasReadings(String ElsterTimestmp){
/*
  1. Læs data fra SD kort, brug kun timestamp
  2. Opsummer antal aflæsninger pr. dag (1 record = 10 liter)
     tilbage til samme dato sidste måned
  3. Opbyg data string
  4. Gem data string på SD-kort
*/
//  debugln("*** buildGasReading() ***");

  String fromDate  = "9999-12-31";
  String currentYY = ElsterTimestmp.substring(0,4);
  String currentMM = ElsterTimestmp.substring(5,7);
  String currentDD = ElsterTimestmp.substring(8,10);
  // Bestem fra-dato, samme dag i sidste måned
  int fromYY = currentYY.toInt();
  int fromMM = currentMM.toInt();
  fromMM -= 1;
  if (fromMM == 0) {  // hvis vi passerer 1. jan baglæns så tæl år ned og sæt måned til dec.
    fromYY -= 1;
    fromMM = 12;
  }
  if (fromMM < 10) {
    fromDate  = (String)fromYY + "-0" + (String)fromMM + "-" + currentDD;
  } else {
    fromDate  = (String)fromYY + "-" + (String)fromMM + "-" + currentDD;
  }

  String dataStr = "{\"0001-01-01\":\"Gas\":0.0}";

  // Gennemlæs records på SD-kortet, opsummer pr. dag fra og med fromDate
  if (!startSDcard()) {
    debugln("   SD card NOT started");
    return dataStr;
  }
  File file = SD.open(logFile);
  if (!file) {
    debugln("   SD log file NOT open");
    return dataStr;
  }
  
  String lineBuffer = "";
  String prevDate = "0001-01-01";
  String dato;
  float  antal = 0.0f;  // 0.000 m3
  char   ch;
  bool   in = false;            // true angiver at vi er i perioden fra og med fromDate

  dataStr = "{";

  while (file.available()) {
    lineBuffer = file.readStringUntil('\n');
    dato = lineBuffer.substring(0,10);
    if (!in && dato >= fromDate) {
      in = true;
      prevDate = dato;
    }
    if (in) {
      antal += GasVolumePrPulse;
      if (dato != prevDate) {         // dato skift
        antal += GasVolumePrPulse;    // tilføj 1 count for 1 værdi - fra sidste dato - for at være i sync med csvfilen
        dataStr += "\"";
        dataStr += (String)prevDate;
        dataStr += "\":{\"Gas\":";
        dataStr += String(antal/1000.0f,3);
        dataStr += "},";
        antal = GasVolumePrPulse;
      }
      prevDate = dato;
    }
  }
  file.close();
  
  dataStr  = dataStr.substring(0,dataStr.length()-1);   // Slet sidste komma
  dataStr += "}";
  debugln("dataStr created:");
  debugln(dataStr);

  char veryLongbuf[1000];
  dataStr.toCharArray(veryLongbuf, 1000);
  writeSD(SD, monthDataFile, veryLongbuf, false);       // Owerwrite file

}
