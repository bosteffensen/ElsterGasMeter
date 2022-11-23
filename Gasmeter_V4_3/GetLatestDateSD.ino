String getLatestDateSD(String readFile) {
 // debugln("In initCurrentm3 : " + currentDate.substring(0, 10));
  String latestDate = "0001-01-01T00:00:00.000Z";
  // if (!SD.begin(PIN_SPI_CS)) {
  //    Serial.println(F("SD CARD FAILED, OR NOT PRESENT!"));
  //    while (1); // don't do anything more:
  //  }
  File file;

  String data = "0001-01-01T00:00:00.000Z";
 // String latestDate = "0001-01-01 00:00:00";
  // open file for reading
  file = SD.open(readFile, FILE_READ);
  if (file) {
    while ( file.available()) {
      data = file.readStringUntil('\n');
      data = data.substring(0, 19);
     
      if (data.compareTo(latestDate) >= 1)
      {
        latestDate = data.substring(0, 19);
      }
    }
    file.close();
  } else {
    Serial.println(F("SD Card: error on opening file"));
    return data;
  }
  latestDate.replace(String(" "),String("T"));
  return latestDate + ".000Z"; //.replace(String(" "),String("T"));
}
