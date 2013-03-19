#include <SoftwareSerial.h>
#include <TinyGPS.h>
#include <util/crc16.h>
#include <SD.h>

// Adafruit GPS - Start Loggin
#define PMTK_STARTLOG  "$PMTK185,0*22"
#define PMTK_ERASE_FLASH "$PMTK184,1*22"

// Timer intervals
long previousMillis = 0;
long interval = 30000; // Read temp & transmit every 30 seconds

//long cutdownInterval = 5400000; // 1hr 30 mins
long cutdownInterval = 200000;
int cutdownPeriod = 10000; // 10 seconds
boolean cutdownComplete = false;

boolean quiet_mode = false;

// Pins
#define RADIOPIN 22   // NTX2 TX
#define GPSBAUD 9600  // Adafruit Ultimate GPS Baud Rate
#define GSMBAUD 9600  // TC35 Baud
#define CUTDOWNPIN 24 // To Relay
#define GSMGROUND 13
#define VOLTAGE_PIN 0

#define modemDelay 500

// Vars
char datastring[60];
String callsign = String("$$RONERY");
String timestamp = "0";
String rtty;
int beacon_count = 0;
float latitude, longitude, altitude, voltage, speed, bearing = 0;
TinyGPS gps;

int SMS_location_number;
const unsigned int MAX_INPUT = 165; // 160 characters for SMS plus a few extra
static unsigned int input_pos = 0;

void setupModem() {
  
// AT+CMGF=1          for txt mode
// AT+CNMI=2,1,0,0,1  message indication
// AT^SMGO=1          SMS full indication
// AT&W               store to memory

  delay(modemDelay);

  //Set the Prefered Message Storage Parameter
  Serial2.println("AT+CMGF=1");
  delay(modemDelay);
  Serial2.println("AT+CNMI=2,1,0,0,1");
  delay(modemDelay);
  Serial2.println("AT^SMGO=1");
  delay(modemDelay);

  //Save Settings to Modem flash memory permanently
  Serial2.println("AT&W");
  delay(modemDelay);
}


void setup() {
  Serial.begin(9600);

  // Setup GPS Baud
  Serial3.begin(GPSBAUD);
  
  // Start GPS Logging
  Serial3.println(PMTK_STARTLOG);

  // Setup Radio
  pinMode(RADIOPIN, OUTPUT);

  // Setup Relay
  pinMode(CUTDOWNPIN, OUTPUT);
  digitalWrite(CUTDOWNPIN, HIGH);
  
  //--- turn on TC35 ---
  // To IGT pin on TC35
  // it grounds IGN pin for 100 ms
  // this is the same as pressing the button
  // on the TC35 to start it up

  pinMode(GSMGROUND, INPUT);
  digitalWrite(GSMGROUND, LOW);
  pinMode(GSMGROUND, OUTPUT);
  delay(100);
  pinMode(GSMGROUND, INPUT);

  // Setup GSM
  Serial2.begin(GSMBAUD);

  setupModem();
  delete_All_SMS();

  Serial.println("Ready.");
}

// Main loop
void loop() {
  // Read voltage
  int val;
  val = analogRead(VOLTAGE_PIN);
  voltage = val * 0.023225;
    
  // Read SMS queue
  readTC35();

  unsigned long currentMillis = millis();

  if (currentMillis > cutdownInterval && !cutdownComplete) {
    Serial.println("Cutting.");
    digitalWrite(CUTDOWNPIN, LOW);
    delay(cutdownPeriod);
    Serial.println("Cutting complete.");
    cutdownComplete = true;
  } else {
    digitalWrite(CUTDOWNPIN, HIGH);
  }

  if (currentMillis - previousMillis > interval) {  
    quiet_mode = true;
    Serial.println("Transmitting.");
    transmit50Baud();
    transmit300Baud();

    Serial.println("Transmission done.");
    quiet_mode = false;

    // Set last, since transmission takes time
    previousMillis = millis();
  }

  if (!quiet_mode) {
    while (Serial3.available() && !quiet_mode) {
      if (gps.encode(Serial3.read())) {
        getgps(gps);
        break;
      }
    }
  }
}

void sendTextMessage(String text) {
  Serial.println("sendTextMessage()");
  Serial.println("Sending SMS.");
  Serial2.print("AT+CMGF=1\r");
  delay(100);
  Serial2.println("AT+CMGS=\"+35312345678\"");
  delay(100);
  Serial2.print(text);
  delay(100);
  Serial2.println((char)26);
}

void sendStatusTextMessage() {
  String text = String();

  text += "Beacon: ";
  text += beacon_count;
  text += ". Altitude: ";
  text += floatToString(altitude, 2);
  text += ". Voltage: ";
  text += floatToString(voltage, 2);
  text += ". Cut-down interval: ";
  text += cutdownInterval;
  text += ". Cut-down Status: ";
  text += cutdownComplete;
  text += ". Map: http://maps.google.com/?q=";
  text += floatToString(latitude, 8);
  text += ",";
  text += floatToString(longitude, 8);
  
  sendTextMessage(text);
}

void transmit50Baud() {
  transmit(50);
}

void transmit300Baud() {
  transmit(300);
}

void transmit(int baud) {
  rtty = String();
  rtty += callsign;
  rtty += ",";
  rtty += beacon_count;
  rtty += ",";
  rtty += timestamp;
  rtty += ",";
  rtty += floatToString(latitude, 8);
  rtty += ",";
  rtty += floatToString(longitude, 8);
  rtty += ",";
  rtty += floatToString(altitude, 2);
  rtty += ",";
  rtty += floatToString(speed, 2); // Speed
  rtty += ",";
  rtty += floatToString(bearing, 2); // Bearing
  rtty += ",";
  rtty += floatToString(voltage, 2); // Temperature - TODO: Battery?
  rtty += ",";
  rtty += cutdownComplete; // Cut-down status
  rtty += ",";
  rtty += cutdownInterval; // Cut-down Interval
  rtty += ",";

  // Convert to character array
  char charBuf[80];
  rtty.toCharArray(charBuf, 80);

  // Calculate checksum
  unsigned int CHECKSUM = gps_CRC16_checksum(charBuf);  // Calculates the checksum for this datastring
  char checksum_str[6];
  sprintf(checksum_str, "*%04X\n", CHECKSUM);

  // Append
  strcat(charBuf, checksum_str);

  // Transmit
  rtty_txstring(baud, charBuf);
  beacon_count = beacon_count + 1;
}

// The getgps function will get and print the values we want.
void getgps(TinyGPS &gps) {
  // To get all of the data into varialbes that you can use in your code,
  // all you need to do is define variables and query the object for the
  // data. To see the complete list of functions see keywords.txt file in
  // the TinyGPS and NewSoftSerial libs.

  //int satCount = TinyGPS::sat_count();
  //Serial.print("Satellites: ");
  //Serial.println(satCount);

  // Define the variables that will be used
  // Then call this function
  gps.f_get_position(&latitude, &longitude);
  // You can now print variables latitude and longitude
//Serial.print("Lat/Long: ");
//Serial.print(latitude, 8);
//  Serial.print(", ");
//  Serial.println(longitude, 8);

  // Same goes for date and time
  int year;
  byte month, day, hour, minute, second, hundredths;
  gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, &hundredths);
  // Print data and time
//  Serial.print("Date: ");
//  Serial.print(month, DEC);
//  Serial.print("/");
//  Serial.print(day, DEC);
//  Serial.print("/");
//  Serial.print(year);
//  Serial.print(" Time: ");
//  Serial.print(hour, DEC);
//  Serial.print(":");
//  Serial.print(minute, DEC);
//  Serial.print(":");
//  Serial.print(second, DEC);
//  Serial.print(".");
//  Serial.println(hundredths, DEC);

  timestamp = String("");
  timestamp += hour;
  timestamp += ":";
  timestamp += minute;
  timestamp += ":";
  timestamp += second;

  //Since month, day, hour, minute, second, and hundr

  // Here you can print the altitude and course values directly since
  // there is only one value for the function
  altitude = gps.f_altitude();
//  Serial.print("Altitude (meters): ");
//  Serial.println(altitude);
  // Same goes for course
  bearing = gps.f_course();
//  Serial.print("Course (degrees): ");
//  Serial.println(bearing);
  // And same goes for speed
  speed = gps.f_speed_kmph();
//  Serial.print("Speed(kmph): ");
//  Serial.println(speed);
//  Serial.println();

  // Here you can print statistics on the sentences.
//  unsigned long chars;
//  unsigned short sentences, failed_checksum;
//  gps.stats(&chars, &sentences, &failed_checksum);
  //Serial.print("Failed Checksums: ");Serial.print(failed_checksum);
  //Serial.println(); Serial.println();
}

void rtty_txstring(int baud, char *string) {
  Serial.print("Transmitting: ");
  Serial.println(string);
  Serial.print("Baud: ");
  Serial.println(baud);

  /* Simple function to sent a char at a time to
    ** rtty_txbyte function.
    ** NB Each char is one byte (8 Bits)
    */

  char c;

  c = *string++;

  while ( c != '\0') {
    rtty_txbyte(baud, c);
    c = *string++;
  }
}


void rtty_txbyte (int baud, char c) {
  /* Simple function to sent each bit of a char to
    ** rtty_txbit function.
    ** NB The bits are sent Least Significant Bit first
    **
    ** All chars should be preceded with a 0 and
    ** proceded with a 1. 0 = Start bit; 1 = Stop bit
    **
    */

  int i;

  rtty_txbit(baud, 0); // Start bit

  // Send bits for for char LSB first

  for (i = 0; i < 7; i++) { // Change this here 7 or 8 for ASCII-7 / ASCII-8
    if (c & 1) rtty_txbit(baud, 1);

    else rtty_txbit(baud, 0);

    c = c >> 1;

  }

  rtty_txbit(baud, 1); // Stop bit
  rtty_txbit(baud, 1); // Stop bit
}

void rtty_txbit (int baud, int bit) {
  if (bit) {
    // high
    analogWrite(RADIOPIN, 138);
  } else {
    // low
    analogWrite(RADIOPIN, 127);
  }

  if (baud == 300) {
    delayMicroseconds(3370); // 300 baud
  } else if (baud == 50) {
    delayMicroseconds(10000); // For 50 Baud uncomment this and the line below.
    delayMicroseconds(10150); // You can't do 20150 it just doesn't work as the
  } else {
    delayMicroseconds(10000); // For 50 Baud uncomment this and the line below.
    delayMicroseconds(10150); // You can't do 20150 it just doesn't work as the
    Serial.println("Unknown Baud - defaulting to 50.");
  }
}

uint16_t gps_CRC16_checksum (char *string) {
  size_t i;
  uint16_t crc;
  uint8_t c;

  crc = 0xFFFF;

  // Calculate checksum ignoring the first two $s
  for (i = 2; i < strlen(string); i++) {
    c = string[i];
    crc = _crc_xmodem_update (crc, c);
  }

  return crc;
}

String floatToString(double number, uint8_t digits) {
  String resultString = "";
  // Handle negative numbers
  if (number < 0.0) {
    resultString += "-";
    number = -number;
  }

  // Round correctly so that print(1.999, 2) prints as "2.00"
  double rounding = 0.5;
  for (uint8_t i = 0; i < digits; ++i)
    rounding /= 10.0;

  number += rounding;

  // Extract the integer part of the number and print it
  unsigned long int_part = (unsigned long)number;
  double remainder = number - (double)int_part;
  resultString += int_part;

  // Print the decimal point, but only if there are digits beyond
  if (digits > 0)
    resultString += ".";

  // Extract digits from the remainder one at a time
  while (digits-- > 0) {
    remainder *= 10.0;
    int toPrint = int(remainder);
    resultString += toPrint;
    remainder -= toPrint;
  }
  return resultString;
}

void readTC35() {

  static char input_line [MAX_INPUT];

  if (Serial2.available() > 0) {
    Serial.println("Serial2.available()");
    while (Serial2.available () > 0) {
      char inByte = Serial2.read();

      switch (inByte) {

      case '\n':   // end of text
        input_line [input_pos] = 0;  // terminating null byte

        // terminator reached! process input_line here ...
        process_data (input_line);

        // reset buffer for next time
        input_pos = 0;
        break;

      case '\r':   // discard carriage return
        break;

      default:
        // keep adding if not full ... allow for terminating null byte
        if (input_pos < (MAX_INPUT - 1))
          input_line [input_pos++] = inByte;
        break;

      }  // end of switch
    }  // end of while incoming data
  }  // end of if incoming data
}  // end of readTC35

void process_data (char * data) {

  // display the data
  Serial.print("*** Data: ");
  Serial.println(data);

  if(strstr(data, "+CMTI:")) {   // An SMS has arrived
    char* copy = data + 12;      // Read from position 12 until a non ASCII number to get the SMS location
    SMS_location_number = (byte) atoi(copy);  // Convert the ASCII number to an int
    Serial2.print("AT+CMGR=");
    Serial2.println(SMS_location_number);  // Print the SMS in Serial Monitor
  }
  
  if(strstr(data, "RING")) {
    Serial.println("Ringing, sending.");
    sendStatusTextMessage();
  }

  if(strstr(data, "clearsms")) {
    delete_All_SMS();
  }

  if(strstr(data, "where")) {
    sendStatusTextMessage();
    Serial.println("where - Sending location.");
  }
  
  if(strstr(data, "erase")) {
    Serial3.println(PMTK_ERASE_FLASH);
    String message = String();
    message += "Flash erased.";
    sendTextMessage(message);
  }
    
  if(strstr(data, "cutdown")) {
    char *str;
    char *p = data;
    while ((str = strtok_r(p, " ", &p)) != NULL) {
      if (!strstr(str, "cutdown")) {
        Serial.print("cutdown interval update. New: ");
        Serial.println(str);
        cutdownInterval = atol(str);
        
        // Reset - in case
        cutdownComplete = false;
        
        String message = String();
        message += "Interval updated, now: ";
        message += str;
        sendTextMessage(message);
      }
    }
    
  }

  if(strstr(data, "^SMGO: 2")) { // SIM card FULL
    delete_All_SMS();           // delete all SMS
  }
}

void delete_one_SMS() {
  Serial.print("deleting SMS ");
  Serial.println(SMS_location_number);
  Serial2.print("AT+CMGD=");
  Serial2.println(SMS_location_number);
}

void delete_All_SMS() {
  for(int i = 1; i <= 20; i++) {
    Serial2.print("AT+CMGD=");
    Serial2.println(i);
    Serial.print("deleting SMS ");
    Serial.println(i);
    delay(500);
  }
}
