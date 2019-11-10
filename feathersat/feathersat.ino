//
// for RFM9x radio
//
#include <SPI.h>			// for RFM9x radio and logger
#include <RH_RF95.h>			// for RFM9x radio
#include "callsign.h"			// for RFM9x radio

#include <Adafruit_GPS.h>		// for GPS

#include <SD.h>				// for logger

#include <Wire.h>			// for temp and accel sensors
#include <Adafruit_Sensor.h>		// for temp and accel sensors
#include <Adafruit_ADT7410.h>		// for temp sensor
#include <Adafruit_ADXL343.h>		// for accel sensor

// from https://learn.adafruit.com/radio-featherwing/wiring#feather-m0-3-10
#define RFM95_CS  10   // "B"
#define RFM95_RST 11   // "A"
#define RFM95_INT  6   // "D"

#define RF95_FREQ 434.0

// Singleton instance of the radio driver
RH_RF95 rf95(RFM95_CS, RFM95_INT);

// what's the name of the hardware serial port?
#define GPSSerial Serial1

// Connect to the GPS on the hardware port
Adafruit_GPS GPS(&GPSSerial);

// Create the ADT7410 temperature sensor object
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();
 
// Create the ADXL343 accelerometer sensor object
Adafruit_ADXL343 accel = Adafruit_ADXL343(12345);

uint32_t timer = millis();

class IterStatus {
  public:
    bool newNMEAreceived;
    bool GPSfix;
    bool GPSparsed;
    uint8_t GPSfixQuality;
    uint8_t GPSfixQuality3d;
    uint8_t GPSsatelliteCount;
    float latitude;
    float longitude;
    float altitude;
    float temperatureC;
    float accelerationX;
    float accelerationY;
    float accelerationZ;

    IterStatus();
    void reset();
};

IterStatus::IterStatus()
{
  this->reset();
}

void IterStatus::reset()
{
  this->newNMEAreceived = false;
  this->GPSfix = false;
  this->GPSparsed = false;
  this->GPSfixQuality = 0;
  this->GPSfixQuality3d = 0;
  this->GPSsatelliteCount = 0;
  this->latitude = 0.0;
  this->longitude = 0.0;
  this->altitude = 0.0;
  this->temperatureC = 1000.0;
  this->accelerationX = 10000.0;
  this->accelerationY = 10000.0;
  this->accelerationZ = 10000.0;
}

// reuse the same IterStatus instance in each loop
IterStatus iterStatus();

void reset_radio()
{
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(10);
}

void setup_radio()
{
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);
  reset_radio();

  while (!rf95.init()) {
    Serial.println("LoRa radio init failed");
    Serial.println("Uncomment '#define SERIAL_DEBUG' in RH_RF95.cpp for detailed debug info");
    while (1);
  }

  Serial.println("LoRa radio init OK!");
 
  // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM
  if (!rf95.setFrequency(RF95_FREQ)) {
    Serial.println("setFrequency failed");
    while (1);
  }

  Serial.print("Set Freq to: ");
  Serial.println(RF95_FREQ);

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on
 
  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then 
  // you can set transmitter powers from 5 to 23 dBm:
  rf95.setTxPower(23, false);
}

void setup_gps()
{
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(9600);

  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);

  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_ANTENNA);

  delay(1000);

  // Ask for firmware version
  GPSSerial.println(PMTK_Q_RELEASE);
}

void setup_logger()
{
  Serial.print("Initializing SD card...");

  if (!SD.begin(4)) {
    Serial.println("initialization failed!");
    while (1);
  }

  Serial.println("initialization done.");
}

void setup_sensors()
{
  Serial.println("Adafruit - ADT7410 + ADX343");
 
  /* Initialise the ADXL343 */
  if(!accel.begin())
  {
    /* There was a problem detecting the ADXL343 ... check your connections */
    Serial.println("Ooops, no ADXL343 detected ... Check your wiring!");
    while(1);
  }
 
  /* Set the range to whatever is appropriate for your project */
  accel.setRange(ADXL343_RANGE_16_G);
 
  /* Initialise the ADT7410 */
  if (!tempsensor.begin())
  {
    Serial.println("Couldn't find ADT7410!");
    while (1)
      ;
  }
 
  // sensor takes 250 ms to get first readings
  delay(250);
}

void setup()
{
  Serial.begin(115200);
  setup_radio();
  setup_gps();
}

void transmit_message(char *message, int message_length)
{
  rf95.send((uint8_t *)message, message_length);
}

void update_gps_data(iterStatus *status)
{
  // read data from the GPS in the 'main loop'
  char c = GPS.read();

  // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    status->newNMEAreceived = true;

    // a tricky thing here is if we print the NMEA sentence, or data
    // we end up not listening and catching other sentences!
    // so be very wary if using OUTPUT_ALLDATA and trying to print out data

    if (!GPS.parse(GPS.lastNMEA())) { // this sets the newNMEAreceived() flag to false
      return; // we can fail to parse a sentence in which case we should just wait for another
  }

  Serial.print("Fix: "); Serial.print((int)GPS.fix);
  Serial.print(" quality: "); Serial.println((int)GPS.fixquality);

  if (GPS.fix) {
    Serial.print("Location: ");
    Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
    Serial.print(", ");
    Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);
    Serial.print("Speed (knots): "); Serial.println(GPS.speed);
    Serial.print("Angle: "); Serial.println(GPS.angle);
    Serial.print("Altitude: "); Serial.println(GPS.altitude);
    Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
  }
}

void log_message(String message)
{
  log_file = SD.open("test.txt", FILE_WRITE);

  if (log_file) {
    log_file.println(message);
    log_file.close();
  }
}

void update_sensor_data()
{
  /* Get a new accel. sensor event */
  sensors_event_t event;
  accel.getEvent(&event);
 
  accelerationX = event.acceleration.x;
  accelerationY = event.acceleration.y;
  accelerationZ = event.acceleration.z;
 
  // Read and print out the temperature
  temperatureC = tempsensor.readTempC();
  Serial.print("Temperature: "); Serial.print(temperatureC); Serial.println("C");
}

void loop()
{
  iterStatus.reset();
  update_gps_data(&status);
  update_sensor_data(&status);
  String message = assemble_message(&status);
  log_message(message);
  transmit_message(message);
  deep_sleep(30);
}
