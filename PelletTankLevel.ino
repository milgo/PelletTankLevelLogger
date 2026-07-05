#include <Ethernet.h>
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
#define MIDNIGHT_INTERVAL 3600//86400L
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


/*unsigned long getTimestampFromTimeServer(const char timeServer[]){

  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011; 
  Udp.beginPacket(timeServer, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();

  delay(1000);
  
  if (Udp.parsePacket()) {
    Udp.read(packetBuffer, NTP_PACKET_SIZE);
    
    // Extract timestamp (seconds since Jan 1 1900)
    unsigned long hiword = word(packetBuffer[40], packetBuffer[41]);
    unsigned long loword = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secs = hiword << 16 | loword;
    // Convert to Unix epoch (seconds since Jan 1 1970)
    unsigned long epoch = secs - 2208988800UL;
    
    Serial.print("Unix time: ");
    Serial.println(epoch);
    return epoch;
  }
  return 0;
}*/

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
          //client.println(F("<g class='labels x-labels'>"));
          //client.println(F("<g>"));

          client.println(F("<g class='labels y-labels' text-anchor='end' font-family='Arial, sans-serif'>"));
          client.println(F("<text x='80' y='15'>100%</text>"));
          client.println(F("<text x='80' y='210'>50%</text>"));
          client.println(F("<text x='80' y='410'>25%</text>"));
          client.println(F("<text x='80' y='610'>0%</text>"));
          client.println(F("<text x='30' y='300' class='label-title' text-anchor='middle' transform='rotate(-90,30,300)' font-size='x-large'>Poziom pelletu w zasobniku</text></g>"));
          client.println(F("<polyline fill='none' stroke='#FF0000' stroke-width='1' marker-start='url(#dot)' marker-mid='url(#dot)' marker-end='url(#dot)' points='"));

          /*time_t epochTime = 1781865455 - 946684800UL;
          struct tm *time_info = localtime(&epochTime );
          char buffer[20];
          strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", time_info);
          client.println(buffer);*/

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
            // Move the internal file pointer to the target position
            if (fileRead.seek(targetPosition)) {
              for(int i=0; i<samplesToRead; i++){
                if (fileRead.read((uint8_t *)&readData, sampleInBytes) == sampleInBytes) {
                    uint16_t level = readData.measurement;
                    if(level > MAX) level = MAX;
                    client.println(String((float)(i*MAX/samplesToRead)+100.0f) + "," + String(level/SCALE_FACTOR));
                }
              }
            }
            fileRead.close();
          }

          client.println(F("'/>"));
          client.println(F("<text x='5' y='650'>level: "));
          client.println(String(getMeasurement()));
          client.println(F(" mm</text></svg>"));
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
  measurementTimeSec = 0;
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
}

void loop() {

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

    for(int i=0; i<MEDIAN_BUF_SIZE; i++)
      mArray[i] = getMeasurement();

    bubbleSort(mArray, MEDIAN_BUF_SIZE);
    
    /*for(int i=0; i<MEDIAN_BUF_SIZE; i++){
      Serial.print(mArray[i]);
      Serial.print(",");
    }
    Serial.println("");*/

    SensorData sensorData;
    sensorData.measurement = mArray[MEDIAN_BUF_SIZE/2];
    //sensorData.timestamp = epoch + measurementTimeSec; //what if epoch == 0?
    //Serial.print(F("Level: "));
    //Serial.println(sensorData.measurement);

    File dataFile = SD.open(DATALOG_FILENAME, FILE_WRITE);
    if (dataFile) {
      dataFile.write((const uint8_t *)&sensorData, sizeof(sensorData));
      dataFile.close();     
    }

    //epoch = 0;
    //Serial.println(F("Time sync expired..."));
    measurementTimeSec = 0;
  }
  
  //sync time 
  /*if(epoch == 0 && syncClockTimeSec - syncClockFailedTimeSec > SYNC_CLOCK_FAILED_TIMEOUT){
    //Serial.println(F("Time syncing..."));
    epoch = getTimestampFromTimeServer(timeServer);
    //measurementTimeSec = 0;
    if(epoch == 0){
      syncClockFailedTimeSec = syncClockTimeSec;
      midnightTomorrow = MIDNIGHT_INTERVAL;
      //Serial.print(F("Time not synced... Midnight tomorow is: "));
      //Serial.println(midnightTomorrow);
    }else{
      midnightTomorrow = (epoch - (epoch % MIDNIGHT_INTERVAL) + MIDNIGHT_INTERVAL);
      //Serial.print(F("Time synced... Midnight tomorrow is: "));
      Serial.println(midnightTomorrow);
      measurementTimeSec = 0;
    }
  }*/
}

