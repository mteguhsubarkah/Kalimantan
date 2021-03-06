/*
1. Mukhtar Amin 
2. Mochamad Teguh Subarkah
*/


//Wiring Guide
//Using Arduino Mega 2560
//LCD 20x4 I2C: SDA -> PIN 20; SCL -> PIN 21; 

//LIBRARIES
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Syergie.h"
#include <Arduino_FreeRTOS.h>
#include <croutine.h>
#include <event_groups.h>
#include <FreeRTOSConfig.h>
#include <FreeRTOSVariant.h>
#include <list.h>
#include <mpu_wrappers.h>
#include <portable.h>
#include <portmacro.h>
#include <projdefs.h>
#include <queue.h>
#include <semphr.h>
#include <stack_macros.h>
#include <task.h>
#include <timers.h>

//PIN DEFINITIONS
#define pwm_right   3
#define pwm_left    5
#define direct_valve_1_1 8
#define direct_valve_1_2 9

#define pin_position_out  A0
#define pinRPM_1          A1
#define pinRPM_2          A2
#define pin_level_tanki   A3

/*-----( Define LCD )-----*/
// set the LCD address to 0x27 for a 20 chars 4 line display
// Set the pins on the I2C chip used for LCD connections:
//                    addr, en,rw,rs,d4,d5,d6,d7,bl,blpol
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address

// Define Tasks
void TaskRpmMeasurement( void *pvParameters );
void TaskMQTT( void *pvParameters );
void task_position_control( void *pvParameters );
void TaskLCD( void *pvParameters );
void TaskRPM( void *pvParameters );

// Update these with values suitable for your network.
byte mac[] = { 0xDE, 0xED, 0xBA, 0xFE, 0xFE, 0xED };
IPAddress ip(123, 45, 0, 11);
IPAddress server(123, 45, 0, 10);

EthernetClient ethClient;
PubSubClient client(ethClient);

/* Fuel Tank Level, Steer, Depth, and PWM*/
int level_tanki = 0;
int steer = 0;
int depth = 0;

int pwm1;
int pwm2;

//Buffers for Publishing via MQTT
char send_rpm_engine[10];
char send_rpm_propeller[10];
char send_tanki[10];
char send_Speed[10];

//Buffers for Displaying to LCD
char lcd_buffer0[21];
char lcd_buffer1[21];
char lcd_buffer2[21];
char lcd_buffer3[21];

/* RPM Measurement */
Proximity speed_1;
Proximity speed_2;
int rpm_1 = 0;
int rpm_2 = 0;
int Speed = 1500;
int pwm_out ;

/* Position Control */
int Position = 1500;


//PID CONSTANTS
double P = 2.5209;
double I = 0.0000293;
double D = 0.2353;

double Buff; // posisi baru
double Last = 0; // posisi lama
double PTerm;
double ITerm;
double DTerm;
double PIDTerm;
double Ts = 1;

double cepat = 0;
double position_out = 0 ;
double position_in = 500;
double position_in_angle = 0;
double Error; // perbedaan feedback dengan input
double Sum = 0; // hasil integral error
//END OF PID CONSTANTS

void setPWMfrequency0(int freq){
   TCCR0B = TCCR0B & 0b11111000 | freq ;
}
void setPWMfrequency1(int freq){
   TCCR1B = TCCR1B & 0b11111000 | freq ;
}
void setPWMfrequency2(int freq){
   TCCR2B = TCCR2B & 0b11111000 | freq ;
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print(F("Message arrived ["));Serial.print(topic);Serial.print(F("] "));
  char data_in[100];
  int value = 0;
  for (int i=0;i<length;i++) { //storing payload to integer value
    data_in[i] = (char)payload[i];
    Serial.print((char)payload[i]);
    value = value*10;
    value=value+(payload[i]-48);
  }
  Serial.println();
  
  if(strcmp(topic,"spc_speed1") == 0){
    client.publish("confirm_SPC1","Received");
    Serial.println("Masuk Speednya Pak Eko");
    Speed = value;
  }

  if(strcmp(topic,"spc_steer1") == 0){
    client.publish("confirm_SPC1","Received");
    Serial.println("Masuk Steernya Pak Eko");
    Position = value;
  }
    
   if(strcmp(topic,"MainControl") == 0){
    client.publish("1EngineSpeed",dtostrf(rpm_1, 4, 0, send_rpm_engine));
    client.publish("1PropellerSpeed",dtostrf(rpm_2, 4, 0, send_rpm_propeller));
    client.publish("1Fuel",dtostrf(level_tanki, 2, 0, send_tanki));
    client.publish("1Box","connected");
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print(F("Attempting MQTT connection... "));
    // Attempt to connect
    if (client.connect("Box1")) {
      Serial.print(server);
      Serial.println(F(" connected"));
      // Once connected, publish an announcement...
      // ... or not...
      // ... and resubscribe
      client.subscribe("spc_speed1");
      client.subscribe("spc_steer1");
      client.subscribe("steer1_kp");
      client.subscribe("steer1_ki");
      client.subscribe("steer1_kd");
      client.subscribe("MainControl");
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(client.state());
      Serial.println(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// the setup function runs once when you press reset or power the board
void setup() {  
  speed_1.pinRPM(pinRPM_1);
  speed_2.pinRPM(pinRPM_2);
  
  // initialize serial communication at 115200 bits per second:
  Serial.begin(115200);
  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB, on LEONARDO, MICRO, YUN, and other 32u4 based boards.
  }

  client.setServer(server, 1883);
  client.setCallback(callback);
  Ethernet.begin(mac, ip);
  
  // Allow the hardware to sort itself out
  delay(1500);
  
  lcd.begin(20,4);         // initialize the lcd for 20 chars 4 lines, turn on backlight

  // ------- Quick 3 blinks of backlight  -------------
  for(int i = 0; i< 3; i++)
  {
    lcd.backlight();
    delay(250);
    lcd.noBacklight();
    delay(250);
  }
  lcd.backlight(); // finish with backlight on  

  //-------- Write characters on the display ------------------
  // NOTE: Cursor Position: Lines and Characters start at 0  
  lcd.setCursor(4,0); //Start at character 4 on line 0
  lcd.print("Salam KAPAL!");
  delay(1000);

  setPWMfrequency0(0x02);// timer 0 , 3.92KHz
  setPWMfrequency1(0x02);// timer 1 , 3.92KHz
  setPWMfrequency2(0x02);// timer 2 , 3.92KHz

  // Now set up tasks to run independently.
  xTaskCreate(
    TaskRpmMeasurement
    ,  (const portCHAR *) "RpmMeasurement"   // A name just for humans
    ,  1024  // This stack size can be checked & adjusted by reading the Stack Highwater, original value 128
    ,  NULL
    ,  1  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,  NULL );

  xTaskCreate(
    TaskMQTT
    ,  (const portCHAR *) "MQTT"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL );

  xTaskCreate(
    task_position_control
    ,  (const portCHAR *) "PID"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL );

    xTaskCreate(
    task_speed_control
    ,  (const portCHAR *) "PID"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL );
    
  xTaskCreate(
    TaskLCD
    ,  (const portCHAR *) "LCD"
    ,  1024  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL );
    
  xTaskCreate(
    TaskRPM
    ,  (const portCHAR *) "RPM"
    ,  128  // Stack size
    ,  NULL
    ,  1  // Priority
    ,  NULL );

  // Now the task scheduler, which takes over control of scheduling individual tasks, is automatically started.
}

void loop(){} // Empty. Things are done in Tasks. 

/*---------------------- Tasks ---------------------*/
void TaskRpmMeasurement(void *pvParameters)  // Task for RPM and Measurements
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countA, countB;
  for (;;) // A Task shall never return or exit.
  {
    countB = millis() - countA; countA = millis();
//    Serial.print(F("Time TaskRPM: "));Serial.println(countB);
    
    rpm_1 = speed_1.calcRPM();
    rpm_2 = speed_2.calcRPM();

    //Steer dan Depth masih dummy
    steer += 10;
    if (steer > 360) {
      steer = 0;
    }
    depth += 13;
    if (depth > 99) {
      depth = 0;
    }
    
    Serial.print(F("RPM 1 : "));Serial.println(rpm_1);
    Serial.print(F("RPM 2 : "));Serial.println(rpm_2);
    
    //Fuel Measurement
    level_tanki = map(analogRead(pin_level_tanki),0,1023,0,20);
    Serial.print(F("Level Tangki : "));Serial.println(level_tanki);
    
    //Governor Control
    switch(Speed){
      case 1900: pwm1 = 200; pwm2 = 0; break; //Speed UP
      case 1500: pwm1 = 0; pwm2 = 0; break; //Steady
      case 1300: pwm1 = 0; pwm2 = 200; break; //Speed Down
    }
    Serial.print(F("Speed : "));Serial.println(Speed);
    Serial.print(F("PWM 1 : "));Serial.println(pwm1);
    Serial.print(F("PWM 2 : "));Serial.println(pwm2);
    analogWrite(pwm_right, pwm1);
    analogWrite(pwm_left, pwm2);
    
    vTaskDelayUntil( &xLastWakeTime, 10);
  }
}

void TaskMQTT(void *pvParameters)  // Task MQTT
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countC, countD;
  for (;;)
  {
    countD = millis() - countC; countC = millis();
//    Serial.print(F("Time TaskMQT: "));Serial.println(countD);
    
    rpm_1 = speed_1.calcRPM();
    rpm_2 = speed_2.calcRPM();
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    
    vTaskDelayUntil( &xLastWakeTime, 10);
  }
}

void task_position_control(void *pvParameters)  // Task PID
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countE, countF;
  for (;;)
  {
    
    countF = millis() - countE; countE = millis();
//    Serial.print(F("Time task_position_control: "));Serial.println(countF);

    rpm_1 = speed_1.calcRPM();
    rpm_2 = speed_2.calcRPM();

    double position_out = analogRead(pin_position_out);
    // Perhitungan PID
    Error = position_in - position_out;
    Buff = position_out; // untuk mencari derivatif
    Sum = Sum + Error; // hasil integral dari error
    PTerm = Error*P; // Proporsional
    ITerm = Sum*I*Ts; // Integral
    DTerm = D*(Last - Buff)/Ts; // Derivatif
    PIDTerm = PTerm + DTerm; // total PID
  
    if (PIDTerm >= 255)
      PIDTerm = 255;
    if (PIDTerm <= -255)
      PIDTerm = -255;
  
    cepat = PIDTerm; // Hasil PID dijadikan data kecepatan
    position_in_angle = map(Position, 1100,1900,0,360);
    position_in = map(position_in, 0,360,250,750);
    Serial.print("Position in :");Serial.println(position_in);
    Serial.print("Position_out : ");Serial.println(analogRead(position_out));
  
    // Jika nilai kecepatan (-), stir berputar ke kanan
    if (cepat < 0) {
      // putar kanan
      int reversePWM = -cepat;
      analogWrite(direct_valve_1_1, 0);
      analogWrite(direct_valve_1_2, reversePWM);
    }
    // Jika nilai kecepatan (+), stir berputar ke kanan
    else if (cepat >= 0) {
      // putar kiri
      int forwardPWM = cepat;
      analogWrite(direct_valve_1_1, forwardPWM);
      analogWrite(direct_valve_1_2, 0);
    }
    Last = position_out; // untuk mencari derivatif
    
    // Delay sebesar 11 tick ~ 198 ms
    vTaskDelayUntil( &xLastWakeTime, 11);
  }
}

void task_speed_control(void *pvParameters)  // Task PID
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countE, countF;
  for (;;)
  {
    Serial.print("PWM out =  ");
    
    
    pwm_out = map(Speed,1100,1900,0,255);
    Serial.println(pwm_out);

    
    // Delay sebesar 11 tick ~ 198 ms
    vTaskDelayUntil( &xLastWakeTime, 11);
  }
}

void TaskLCD(void *pvParameters)  // Task LCD
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countG, countH;
  for (;;)
  {
    countH = millis() - countG; countG = millis();
//    Serial.print(F("Time TaskLCD: "));Serial.println(countH);

    rpm_1 = speed_1.calcRPM();
    rpm_2 = speed_2.calcRPM();

    sprintf((char*) lcd_buffer0, "RPM Engine: %4d    ", rpm_1);
    sprintf((char*) lcd_buffer1, "RPM Propeller: %4d ", rpm_2);
    sprintf((char*) lcd_buffer2, "Level Tangki: %2d L  ", level_tanki);
    sprintf((char*) lcd_buffer3, "Steer:%3d%c Depth:%2dm", steer, 223, depth);

    lcd.setCursor(0,0); //Start at character 0 on line 0
    lcd.print(lcd_buffer0);
    lcd.setCursor(0,1);
    lcd.print(lcd_buffer1);
    lcd.setCursor(0,2);
    lcd.print(lcd_buffer2);
    lcd.setCursor(0,3);
    lcd.print(lcd_buffer3);

    // Delay sebesar 28 tick, 1 tick ~ 18 ms
    vTaskDelayUntil( &xLastWakeTime, 28);//( 120 / portTICK_PERIOD_MS ) );
  }
}

void TaskRPM(void *pvParameters)  // Task khusus RPM
{
  (void) pvParameters;
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  
  int countG, countH;
  for (;;)
  {
    rpm_1 = speed_1.calcRPM();
    rpm_2 = speed_2.calcRPM();

    vTaskDelayUntil( &xLastWakeTime, 1);
  }
}
