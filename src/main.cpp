// *******************************************************************
//  Arduino Nano 5V example code
//  for   https://github.com/EmanuelFeru/hoverboard-firmware-hack-FOC
//
//  Copyright (C) 2019-2020 Emanuel FERU <aerdronix@gmail.com>
//
// *******************************************************************
// INFO:
// • This sketch uses the the Serial Software interface to communicate and send commands to the hoverboard
// • The built-in (HW) Serial interface is used for debugging and visualization. In case the debugging is not needed,
//   it is recommended to use the built-in Serial interface for full speed perfomace.
// • The data packaging includes a Start Frame, checksum, and re-syncronization capability for reliable communication
// 
// The code starts with zero speed and moves towards +
//
// CONFIGURATION on the hoverboard side in config.h:
// • Option 1: Serial on Right Sensor cable (short wired cable) - recommended, since the USART3 pins are 5V tolerant.
// #define CONTROL_SERIAL_USART3
//   #define FEEDBACK_SERIAL_USART3
//   // #define DEBUG_SERIAL_USART3
// • Option 2: Serial on Left Sensor cable (long wired cable) - use only with 3.3V devices! The USART2 pins are not 5V tolerant!
#define CONTROL_SERIAL_USART2
#define FEEDBACK_SERIAL_USART2
#define DEBUG_SERIAL_USART2
// *******************************************************************

// ########################## DEFINES ##########################
#define HOVER_SERIAL_BAUD   115200      // [-] Baud rate for HoverSerial (used to communicate with the hoverboard)
#define SERIAL_BAUD         115200      // [-] Baud rate for built-in Serial (used for the Serial Monitor)
#define START_FRAME         0xABCD     	// [-] Start frme definition for reliable serial communication
#define TIME_SEND           100         // [ms] Sending time interval
#define SPEED_MAX_TEST      300         // [-] Maximum speed for testing
#define SPEED_STEP          20          // [-] Speed step
#define DEBUG_RX                        // [-] Debug received data. Prints all bytes to serial (comment-out to disable)

#include <Arduino.h>
#ifdef ESP32
  //WIFI Libs
  // #include <AsyncTCP.h>
  // #include <WiFi.h>
  // #include <DNSServer.h>
  // #include <ESPAsyncWebServer.h>
  // #include <WString.h>
  #include <WebSerial.h>

  AsyncWebServer  server(80);
  //ESP Storage Lib
  #include <SPIFFS.h>
  /*
  * There are three serial ports on the ESP known as U0UXD, U1UXD and U2UXD.
  * 
  * U0UXD is used to communicate with the ESP32 for programming and during reset/boot.
  * U1UXD is unused and can be used for your projects. Some boards use this port for SPI Flash access though
  * U2UXD is unused and can be used for your projects.
  * 
  */
  HardwareSerial & HoverSerial = Serial2;
#else
  #include <SoftwareSerial.h>
  SoftwareSerial HoverSerial(2,3);        // RX, TX
#endif
HardwareSerial & Monitoring = Serial;

const char* ssid = "teveo"; // WiFi AP SSID
const char* password = "p4th3tik0"; // WiFi AP Password

static uint32_t last = millis();

// Global variables
uint8_t idx = 0;                        // Index for new data pointer
uint16_t bufStartFrame;                 // Buffer Start Frame
byte *p;                                // Pointer declaration for the new received data
byte incomingByte;
byte incomingBytePrev;

typedef struct{
   uint16_t start;
   int16_t  steer;
   int16_t  speed;
   uint16_t checksum;
} SerialCommand;
SerialCommand Command;

typedef struct{
   uint16_t start;
   int16_t  cmd1;
   int16_t  cmd2;
   int16_t  speedR_meas;
   int16_t  speedL_meas;
   int16_t  batVoltage;
   int16_t  boardTemp;
   uint16_t cmdLed;
   uint16_t checksum;
} SerialFeedback;
SerialFeedback Feedback;
SerialFeedback NewFeedback;

void wifi_setup(){
  WiFi.softAP(ssid, password);
  Monitoring.print("IP Address: ");
  Monitoring.println(WiFi.softAPIP().toString());

  // WebSerial.onMessage([](const String& msg) { Monitoring.println(msg); });
  WebSerial.begin(&server);

  server.onNotFound([](AsyncWebServerRequest* request) { request->redirect("/webserial"); });
  
    /* Attach Message Callback */
  WebSerial.onMessage([&](uint8_t *data, size_t len) {
    Monitoring.printf("Received %u bytes from WebSerial: ", len);
    Monitoring.write(data, len);
    Monitoring.println();
    WebSerial.println("Received Data...");
    String d = "";
    for(size_t i=0; i < len; i++){
      d += char(data[i]);
    }
    WebSerial.println(d);
    // Send(0,)
  });

  server.begin();
}

// ########################## SETUP ##########################
void setup() 
{
  Monitoring.begin(SERIAL_BAUD);
  Monitoring.println("Hoverboard Serial v1.0");
  HoverSerial.begin(HOVER_SERIAL_BAUD);

  wifi_setup();
  pinMode(LED_BUILTIN, OUTPUT);
}

// ########################## SEND ##########################
void Send(int16_t uSteer, int16_t uSpeed)
{
  // Create command
  Command.start    = (uint16_t)START_FRAME;
  Command.steer    = (int16_t)uSteer;
  Command.speed    = (int16_t)uSpeed;
  Command.checksum = (uint16_t)(Command.start ^ Command.steer ^ Command.speed);

  // Write to Serial
  HoverSerial.write((uint8_t *) &Command, sizeof(Command)); 
}

// ########################## RECEIVE ##########################
void Receive()
{
    // Check for new data availability in the Serial buffer
    if (HoverSerial.available()) {
        incomingByte 	  = HoverSerial.read();                                   // Read the incoming byte
        bufStartFrame	= ((uint16_t)(incomingByte) << 8) | incomingBytePrev;       // Construct the start frame
    }
    else {
        return;
    }

    // If DEBUG_RX is defined print all incoming bytes
    #ifdef DEBUG_RX
        Monitoring.print(incomingByte);
        return;
    #endif

    // Copy received data
    if (bufStartFrame == START_FRAME) {	                    // Initialize if new data is detected
        p       = (byte *)&NewFeedback;
        *p++    = incomingBytePrev;
        *p++    = incomingByte;
        idx     = 2;	
    } else if (idx >= 2 && idx < sizeof(SerialFeedback)) {  // Save the new received data
        *p++    = incomingByte; 
        idx++;
    }	
    
    // Check if we reached the end of the package
    if (idx == sizeof(SerialFeedback)) {
        uint16_t checksum;
        checksum = (uint16_t)(NewFeedback.start ^ NewFeedback.cmd1 ^ NewFeedback.cmd2 ^ NewFeedback.speedR_meas ^ NewFeedback.speedL_meas
                            ^ NewFeedback.batVoltage ^ NewFeedback.boardTemp ^ NewFeedback.cmdLed);

        // Check validity of the new data
        if (NewFeedback.start == START_FRAME && checksum == NewFeedback.checksum) {
            // Copy the new data
            memcpy(&Feedback, &NewFeedback, sizeof(SerialFeedback));

            // Print data to built-in Serial
           Monitoring.print("1: ");  Monitoring.print(Feedback.cmd1);
           Monitoring.print(" 2: "); Monitoring.print(Feedback.cmd2);
           Monitoring.print(" 3: "); Monitoring.print(Feedback.speedR_meas);
           Monitoring.print(" 4: "); Monitoring.print(Feedback.speedL_meas);
           Monitoring.print(" 5: "); Monitoring.print(Feedback.batVoltage);
           Monitoring.print(" 6: "); Monitoring.print(Feedback.boardTemp);
           Monitoring.print(" 7: "); Monitoring.println(Feedback.cmdLed);
        } else {
         Monitoring.println("Non-valid data skipped");
        }
        idx = 0;    // Reset the index (it prevents to enter in this if condition in the next cycle)
    }

    // Update previous states
    incomingBytePrev = incomingByte;
}

// ########################## LOOP ##########################

unsigned long iTimeSend = 0;
int16_t iTest = 0;
int iStep = SPEED_STEP;

void loop(void)
{ 
  unsigned long timeNow = millis();

  // Check for new received data
  Receive();

  // Send commands
    if (iTimeSend > timeNow) return;
    if (millis() - last > 50) {

    
    if (iTest >= SPEED_MAX_TEST) iTest = SPEED_MAX_TEST;
    if (iTest <= -SPEED_MAX_TEST) iTest = -SPEED_MAX_TEST;
    // WebSerial.print(buffer);

    Send( 0 , iTest ); // usteer, uspeed  int_16t
    last = millis();
  }
  WebSerial.loop();
  // Blink the LED
  digitalWrite(LED_BUILTIN, (last%2000)<1000);
}