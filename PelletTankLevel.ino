#include <Ethernet.h>
#include <virtuabotixRTC.h>
//#include <EthernetUdp.h>
#include <SD.h>
#include <SPI.h>
//#include <time.h>

#define MAX 900.0f
#define SCALE_FACTOR 1.5f
#define DATALOG_FILENAME "data.log"
#define ENABLE_PIN 2
#define READ_PIN 3
//#define SYNC_CLOCK_FAILED_TIMEOUT 300 //should be ~300s
#define MIDNIGHT_INTERVAL 86400L
#define MEDIAN_BUF_SIZE 11
#define MAX_SAMPLES_TO_READ 30
//#define BYTES_TO_READ 240
#define SD_CHIP_SELECT 4
//#define SYNC_CLOCK_EXPIRE_TIME 3000 
//#define NEXT_MEASUREMENT_TIME 1000 

// NTP Server settings
//const int NTP_PACKET_SIZE = 48;
//byte packetBuffer[NTP_PACKET_SIZE];

// MAC address for controller, UDP setup
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
//unsigned int localPort = 8888;

//EthernetUDP Udp;
IPAddress ip(192, 168, 1, 80); // IP address, may need to change depending on network
//const char timeServer[] = "tempus1.gum.gov.pl";
EthernetServer server(80);  // create a server at port 80
virtuabotixRTC myRTC(6, 7, 8);

unsigned long lastTimeMs = 0;
unsigned long timeMs = 0;
unsigned long measurementTimeSec = 0;
//unsigned long measurementTimeSec = 0;
//unsigned long syncClockTimeSec = 0;
//unsigned long syncClockFailedTimeSec = 0;
unsigned long midnightTomorrow = 0;
//unsigned long epoch = 0;

struct SensorData {
//  uint32_t timestamp;
  uint8_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint16_t measurement;
};

uint16_t mArray[MEDIAN_BUF_SIZE];

void bubbleSort(int a[], int size){
  for(int i=0;i<(size-1);i++){
    for(int j=0;j<(size-(i+1));j++){
      if(a[j]>a[j+1]){
        int t = a[j];
        a[j] = a[j+1];
        a[j+1] = t;
      }
    }
  }
}

unsigned int getMeasurement(){
  unsigned int readByte = 0;
  digitalWrite(ENABLE_PIN, HIGH);
  for (int i=0; i<16; i++) 
    if(pulseIn(READ_PIN, LOW, 1000) <= 20) 
      readByte = bitSet(readByte, i);
  digitalWrite(ENABLE_PIN, LOW);
  delay(200);
  return readByte;
}

void provideWepPage(){
  EthernetClient client = server.available();
  File fileRead;
    //serve datalog as webpage
  if (client) {  // got client?
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {   // client data available to read
        char c = client.read(); // read 1 byte (character) from client
        // last line of client request is blank and ends with \n
        // respond to client only after last line received
        if (c == '\n' && currentLineIsBlank) {

          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          // send web page
          client.println(F("<html><head></head><body><svg viewBox='0 0 1200 800' class='chart' style='background-color: #ffffff;'>"));
          client.println(F("<defs><marker id='dot' markerWidth='10' markerHeight='10' refX='5' refY='5'><circle cx='5' cy='5' r='4' fill='red'/></marker></defs>"));
          client.println(F("<title id='title'>Pallet tank level</title>"));
          client.println(F("<g class='grid x-grid' id='xGrid'>"));
          client.println(F("<line x1='90' x2='90' y1='0' y2='600' stroke='black' stroke-width='1' stroke-linecap='butt'/></g>"));
          client.println(F("<g class='grid y-grid' id='yGrid'>"));
          client.println(F("<line x1='90' x2='1000' y1='600' y2='600' stroke='black' stroke-width='1' stroke-linecap='butt'/></g>"));

          client.println(F("<g stroke='#ccc' stroke-width='1' stroke-linecap='butt' stroke-dasharray='5,5'>"));
          client.println(F("<line x1='80' x2='1000' y1='475' y2='475' />"));//1bag
          client.println(F("<line x1='80' x2='1000' y1='400' y2='400' />"));//2bag
          client.println(F("<line x1='80' x2='1000' y1='355' y2='355' />"));//3bag
          client.println(F("<line x1='80' x2='1000' y1='310' y2='310' />"));//4bag
          client.println(F("<line x1='80' x2='1000' y1='265' y2='265' />"));//5bag
          client.println(F("<line x1='80' x2='1000' y1='220' y2='220' />"));//6bag
          client.println(F("<line x1='80' x2='1000' y1='175' y2='175' />"));//7bag
          client.println(F("<line x1='80' x2='1000' y1='130' y2='130' />"));//8bag
          client.println(F("<line x1='80' x2='1000' y1='85' y2='85' />"));//9bag
          client.println(F("<line x1='80' x2='1000' y1='40' y2='40' />"));//10bag
          client.println(F("</g>"));

          client.println(F("<g><text x='30' y='300' class='label-title' text-anchor='middle' transform='rotate(-90,30,300)' font-size='x-large'>Poziom pelletu w zasobniku</text></g>"));
          client.println(F("<polyline fill='none' stroke='#FF0000' stroke-width='1' marker-start='url(#dot)' marker-mid='url(#dot)' marker-end='url(#dot)' points='"));

          fileRead = SD.open(DATALOG_FILENAME, FILE_READ);
          if (fileRead) {
            uint32_t fileSize = fileRead.size();
            SensorData readData;
            uint8_t sampleInBytes = sizeof(readData);
            uint16_t bytesToRead = MAX_SAMPLES_TO_READ * sampleInBytes;
            if(bytesToRead > fileSize) 
              bytesToRead = (int)(fileSize / sampleInBytes) * sampleInBytes;
            uint8_t samplesToRead = bytesToRead / sampleInBytes;

            // Calculate the starting position (End of file minus X bytes)
            uint32_t targetPosition = fileSize - bytesToRead;
            uint8_t readDay = -1;
            // Move the internal file pointer to the target position
            if (fileRead.seek(targetPosition)) {
              for(int i=0; i<samplesToRead; i++){
                if (fileRead.read((uint8_t *)&readData, sampleInBytes) == sampleInBytes) {
                                   
                    if(readDay > 0 && readData.day != readDay){
                      uint16_t level = readData.measurement;   
                      if(level > MAX) level = MAX;
                      client.println(String((float)(i*MAX/samplesToRead)+100.0f) + "," + String(level/SCALE_FACTOR));
                    }
                    
                    readDay = readData.day;
                }
              }
            }
            fileRead.close();
          }

          client.println(F("'/>"));
          client.println(F("<text x='5' y='650'>"));
          client.println(String(myRTC.year) + "-" + String(myRTC.month) + "-" + String(myRTC.dayofmonth) + " " + String(myRTC.hours) + ":" + String(myRTC.minutes) + ", range: " + String(getMeasurement()) + "mm, sec to midnight: " + String((uint32_t)(midnightTomorrow - measurementTimeSec)) + "sec.");
          client.println(F("</text></svg>"));
          client.println(F("</body></html>"));

          break;
        }
        // every line of text received from the client ends with \r\n
        if (c == '\n') {
          // last character on line of received text
          // starting new line with next character read
          currentLineIsBlank = true;
        } 
        else if (c != '\r') {
          // a text character was received from client
          currentLineIsBlank = false;
        }
      } // end if (client.available())
    } // end while (client.connected())
    delay(1);      // give the web browser time to receive the data
    client.stop(); // close the connection
  } // end if (client)  
}

void setup() {
  timeMs = 0;
  lastTimeMs = 0;
  //syncClockFailedTimeSec = 0;
  measurementTimeSec = MIDNIGHT_INTERVAL; //measure when power-on
  midnightTomorrow = MIDNIGHT_INTERVAL;
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(READ_PIN, INPUT);
  Serial.begin(9600);
  Ethernet.begin(mac, ip);
  //Udp.begin(localPort);

  if (!SD.begin(4)) {
    Serial.println(F("SD Error"));
    while (1);
  }

  //myRTC.setDS1302Time(00, 38, 23, 4, 9, 7, 2026);
}

void loop() {

  myRTC.updateTime();
  timeMs = millis(); // Pobranie aktualnego czasu
  if(timeMs - lastTimeMs > 1000){
    measurementTimeSec++;
    //Serial.print("Measuring count: ");
    //Serial.println(measurementTimeSec);
    //syncClockTimeSec++;
    lastTimeMs = timeMs;
  }

  provideWepPage();

  //time for new measurement
  if(/*epoch + */measurementTimeSec >= midnightTomorrow){
    //Serial.println(F("Measuring level..."));

    //check if last logger measurement was already taken today
    bool measuredToday = false;
    File fileRead = SD.open(DATALOG_FILENAME, FILE_READ);
    if (fileRead) {
      uint32_t fileSize = fileRead.size();
      SensorData readData;
      if (fileRead.seek(fileSize - sizeof(readData))) {
        if (fileRead.read((uint8_t *)&readData, sizeof(readData)) == sizeof(readData)) {
          if(myRTC.dayofmonth == readData.day){
            measuredToday = true;
          }
        }
      }
      fileRead.close();
    }

    if(measuredToday == false){

      for(int i=0; i<MEDIAN_BUF_SIZE; i++)
        mArray[i] = getMeasurement();

      bubbleSort(mArray, MEDIAN_BUF_SIZE);

      SensorData sensorData;
      sensorData.year = myRTC.year-2000;
      sensorData.month = myRTC.month;
      sensorData.day = myRTC.dayofmonth;
      sensorData.hour = myRTC.hours;
      sensorData.minute = myRTC.minutes;

      if(mArray[MEDIAN_BUF_SIZE/2] < MAX){ 
        sensorData.measurement = mArray[MEDIAN_BUF_SIZE/2];

        File dataFile = SD.open(DATALOG_FILENAME, FILE_WRITE);
        if (dataFile) {
          dataFile.write((const uint8_t *)&sensorData, sizeof(sensorData));
          dataFile.close();     
        }

        measurementTimeSec = 0;
        unsigned long todaySec = (unsigned long)myRTC.hours * 3600L;
        todaySec += (unsigned long)myRTC.minutes * 60L;
        todaySec += (unsigned long)myRTC.seconds;
        unsigned long mt = MIDNIGHT_INTERVAL - todaySec; //calculate next measure time on midnight
        if(mt < 0)
          mt = 60;
        midnightTomorrow = mt;
      }
    }
  }
}

