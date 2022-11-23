void Gas_m3_PrDay(String currentDate) // "2022-12-31 12:00:00"
{
  //static unsigned int currentM3 = 0;
  // debugln("xx");
  if (currentDate.substring(0, 10) != currentDay)
  { // dato er skiftet...
    String saveStr = currentDay.c_str();
    saveStr.concat(";" + (String)currentM3) ;

    debugln(saveStr);
    //    debugln("yy");
    // save current sum and make new sum
    //sdrewrite.... currentDay,currentM3
    if (currentDay.compareTo("null"))  // Only save if true date - null is first time after restart
    {
      debugln(currentDay.compareTo("null"));
      debug ("GRRR : ");
      debugln(currentDay);
      saveStr.toCharArray(longbuf, 100);
      writeSD(SD, gasPrDayFile, longbuf, true);  // append file, so it holds all records
    }
    else
    {
      debugln("PPP");
    }
    currentM3 = 10; //reinitialize due to new date
    currentDay = currentDate.substring(0, 10);
    debugln("Sum reinit");

    buildGasReadings(currentDate);       // Dan data til bar-graf

  }
  else
  {
    // Same day sum++;
    currentM3 += GasVolumePrPulse;
    debugln(currentM3);
  }
}

// ElsterTimestmp + " " + GAS.substring(0, (GAS.length() - 3)) + "," + GAS.substring(GAS.length() - 3) + " m3 Today: " + currentM3;
String GasUsrStr(String date, unsigned long elsterCounter, int  currentM3)
{
  String formattetGAS = date;
  return formattetGAS + " " + formatL(elsterCounter) + " m3 <BR> Today: " + formatL(currentM3) + " m3";
}


String formatL(unsigned long value) {    // returnerer 0.000 for 0 as input or 14956.234 m3
  String xx = String(value);
  switch (xx.length()) {
    case 1:
      xx = "0.00" + xx;
      break;
    case 2:
      xx = "0.0" + xx;
      break;
    case 3:
      xx = "0." + xx;
      break;
    default:
      xx = xx.substring(0, (xx.length() - 3)) + "." + xx.substring(xx.length() - 3);
  }

  return xx;
}
int initCurrentM3(String readFile, String currentDate) {
  debugln("In initCurrentm3 : " + currentDate.substring(0, 10));
  unsigned int currentM3 = 0;
  // if (!SD.begin(PIN_SPI_CS)) {
  //    Serial.println(F("SD CARD FAILED, OR NOT PRESENT!"));
  //    while (1); // don't do anything more:
  //  }
  File file;
  String data;
  // open file for reading
  file = SD.open(readFile, FILE_READ);
  if (file) {
    while (file.available()) {
      data = file.readStringUntil('\n');
      if (data.startsWith(currentDate.substring(0, 10))) {
        currentM3 += GasVolumePrPulse;
      }
    }
    file.close();
  } else {
    debug(F("SD Card: error on opening file"));
  }
  return currentM3;
}
