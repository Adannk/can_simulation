#define controlPin1 8                                               //Define external pins
#define controlPin2 9
#define enablePin 10
#define potetionMeterPin A0
#define ventLEDGood 12
#define ventLEDBroken 11
#define fuelLED100 7
#define fuelLED80 6
#define fuelLED60 5
#define fuelLED40 4
#define fuelLED20 3
#define buttonSwitch 2

#include <Arduino_FreeRTOS.h>                                       //Import libraries
#include "queue.h"
#include "semphr.h"

QueueHandle_t iQueueString;                                         //Variables to control communication between tasks
SemaphoreHandle_t xMutex;

TickType_t xTickToWait = pdMS_TO_TICKS(1000);                       //Constant delay time

//Had to put the status of ventilation as global to simulate vent breakdown with ISR Button
volatile bool ventBroken = false;

typedef struct{                                                      //Struct that holds all car variables
  bool motorBroken;
  bool gearboxBroken;
  uint8_t motorEnable;
  uint16_t motorSpeed;
  uint8_t motorDirection;
  float fuelLevel;
}sCarObject;

sCarObject sCar1 = {false, false, 1, 0, 1, 100};                     //Car object we use in the program


void setup(){
  pinMode(controlPin1, OUTPUT);                                      //Setup pinMode for external pins
  pinMode(controlPin2, OUTPUT);
  pinMode(enablePin, OUTPUT);
  pinMode(ventLEDGood, OUTPUT);
  pinMode(ventLEDBroken, OUTPUT);
  pinMode(fuelLED100, OUTPUT);
  pinMode(fuelLED80, OUTPUT);
  pinMode(fuelLED60, OUTPUT);
  pinMode(fuelLED40, OUTPUT);
  pinMode(fuelLED20, OUTPUT);
  pinMode(buttonSwitch, INPUT);
  attachInterrupt(digitalPinToInterrupt(buttonSwitch), breakVentISR, FALLING);
  digitalWrite(enablePin, LOW);

  Serial.begin(9600);
  while (!Serial) {}
  
  xMutex = xSemaphoreCreateMutex();                                 //Mutex to protect and control valuable functions Serial.print(); and Serial.prinl(); which are very slow

  iQueueString = xQueueCreate(5,sizeof(String));                    //Queue thats responsible for communication between tasks

  xTaskCreate(motor, "mainMotor Task", 128, &sCar1, 1, NULL);       //Creating my 3 tasks
  xTaskCreate(ventilation, "Ventilation Task", 128, &sCar1, 1, NULL);
  xTaskCreate(fuel, "Fuel Task", 128, &sCar1, 1, NULL);
}

void motor(void *pvParameters){                                     //Takes void* pointer as parameter
  sCarObject* sLocalCarObject = (sCarObject*)pvParameters;
  String payloadToRcv;                                              //Status and and message from fuel and vent
  
  while(1){
    if(sLocalCarObject->fuelLevel > 0){
      sLocalCarObject->motorSpeed = analogRead(potetionMeterPin) / 4;
    }
    uint16_t RPM = sLocalCarObject->motorSpeed * 15;
    uint16_t speed = sLocalCarObject->motorSpeed / 3;

    if(sLocalCarObject->motorDirection == 1)                        //Code that controls the motor speed and direction
    {
      digitalWrite(controlPin1, HIGH);
      digitalWrite(controlPin2, LOW);
    }
    else
    {
      digitalWrite(controlPin1, LOW);
      digitalWrite(controlPin2, HIGH);
    }
    if((sLocalCarObject->motorEnable == 1) && (sLocalCarObject->fuelLevel >= 0) && (sLocalCarObject->motorSpeed >= 1))
    {
      analogWrite(enablePin, sLocalCarObject->motorSpeed);
      sLocalCarObject->fuelLevel--;
    }
    else
    {
      analogWrite(enablePin, LOW);
      sLocalCarObject->motorSpeed = 0;
    }

    if (sLocalCarObject->motorBroken == true){                      //Checking engine and gearbox and sends output to shared dashboard
      shareded_dashboard("x01:Error: M.\n");
    }
    if (sLocalCarObject->gearboxBroken == true){
      shareded_dashboard("x01:Error: Gb.\n");
    }
    if ((sLocalCarObject->motorBroken == true) && (sLocalCarObject->gearboxBroken == false)){
      shareded_dashboard("G is ok, \n");
    }
    if ((sLocalCarObject->motorBroken == false) && (sLocalCarObject->gearboxBroken == true)){
      shareded_dashboard("M is ok, \n");
    }
    else if ((sLocalCarObject->motorBroken == false) && (sLocalCarObject->gearboxBroken == false)){
      shareded_dashboard("M.G is ok, \n");
    }

    shareded_dashboard("Speed ");                                   //Prints out speed and rpm
    shareded_dashboardInt(speed);
    shareded_dashboard(" and rpm is ");
    shareded_dashboardInt(RPM);
    shareded_dashboard("\n");

    xQueueReceive(iQueueString,&payloadToRcv,xTickToWait);          //Reciving status from ventialtion task threw queue and sends output to shared dashboard
    if(payloadToRcv == "x02:Error: Vent"){
      shareded_dashboard(payloadToRcv);
      shareded_dashboard("\n");
    }
    else if (payloadToRcv = "Vent. Is ok"){
      shareded_dashboard(payloadToRcv);
      shareded_dashboard("\n");
    }

    xQueueReceive(iQueueString,&payloadToRcv,xTickToWait);          //Reciving status from fuel task threw queue and sends output to shared dashboard
    if(payloadToRcv == "0x3: low fuel"){
      shareded_dashboard(payloadToRcv);
      shareded_dashboard("\n");
    }
    else{
      shareded_dashboard("Fuel. is ");
      shareded_dashboardInt(sLocalCarObject->fuelLevel);
      shareded_dashboard("%\n");
      shareded_dashboard(payloadToRcv);
      shareded_dashboard("\n");
    }

    vTaskDelay(xTickToWait);
  }
}

void ventilation(void* pvParameters){
  String badVent = "x02:Error: Vent";
  String goodVent = "Vent. Is ok";
  
  while(1){                                                           //Sends status from ventilation task to motor task
    if (ventBroken == true){
      xQueueSend(iQueueString,&badVent,xTickToWait);
      digitalWrite(ventLEDGood, LOW);
      digitalWrite(ventLEDBroken, HIGH);
      shareded_dashboard("N*N*\n");
    }
    else{
      xQueueSend(iQueueString,&goodVent,xTickToWait);
      digitalWrite(ventLEDGood, HIGH);
      digitalWrite(ventLEDBroken, LOW);
      shareded_dashboard("Y*Y*\n");
    }
    vTaskDelay(xTickToWait);    
  }  
}

void fuel(void* pvParameters){
  sCarObject* sLocalCarObject = (sCarObject*)pvParameters;
  String lowFuel = "0x3: low fuel";
  String goodFuel = "0x4 good fuel";

  while(1){                                                         //Control fuel LEDS
    if((sLocalCarObject->fuelLevel <= 100) && (sLocalCarObject->fuelLevel > 80)){
      digitalWrite(fuelLED100, HIGH);
      digitalWrite(fuelLED80, HIGH);
      digitalWrite(fuelLED60, HIGH);
      digitalWrite(fuelLED40, HIGH);
      digitalWrite(fuelLED20, HIGH);
    }
    if((sLocalCarObject->fuelLevel <= 80) && (sLocalCarObject->fuelLevel > 60)){
      digitalWrite(fuelLED100, LOW);
      digitalWrite(fuelLED80, HIGH);
      digitalWrite(fuelLED60, HIGH);
      digitalWrite(fuelLED40, HIGH);
      digitalWrite(fuelLED20, HIGH);
    }
    if((sLocalCarObject->fuelLevel <= 60) && (sLocalCarObject->fuelLevel > 40)){
      digitalWrite(fuelLED100, LOW);
      digitalWrite(fuelLED80, LOW);
      digitalWrite(fuelLED60, HIGH);
      digitalWrite(fuelLED40, HIGH);
      digitalWrite(fuelLED20, HIGH);
    }
    if((sLocalCarObject->fuelLevel <= 40) && (sLocalCarObject->fuelLevel > 20)){
      digitalWrite(fuelLED100, LOW);
      digitalWrite(fuelLED80, LOW);
      digitalWrite(fuelLED60, LOW);
      digitalWrite(fuelLED40, HIGH);
      digitalWrite(fuelLED20, HIGH);
    }
    if(sLocalCarObject->fuelLevel <= 20){
      digitalWrite(fuelLED100, LOW);
      digitalWrite(fuelLED80, LOW);
      digitalWrite(fuelLED60, LOW);
      digitalWrite(fuelLED40, LOW);
      digitalWrite(fuelLED20, HIGH);
    }
    if(sLocalCarObject->fuelLevel <= 0){
      digitalWrite(fuelLED100, LOW);
      digitalWrite(fuelLED80, LOW);
      digitalWrite(fuelLED60, LOW);
      digitalWrite(fuelLED40, LOW);
      digitalWrite(fuelLED20, LOW);
    }

    if(sLocalCarObject->fuelLevel <= 10.00){                        //Sends status from fuel task to motor task
      xQueueSend(iQueueString,&lowFuel,xTickToWait);
      shareded_dashboard("U$U$\n");
      
    }
    else{
      xQueueSend(iQueueString,&goodFuel,xTickToWait);
      shareded_dashboard("h$h$\n");
    }
    vTaskDelay(xTickToWait);    
  }  
}

void shareded_dashboard(String stringToPrint){                      //Function to control String prints to Serial
  xSemaphoreTake(xMutex, xTickToWait);
  Serial.print(stringToPrint);
  xSemaphoreGive(xMutex);
}

void shareded_dashboardInt(int intToPrint){                        //Function to control Int/float prints to Serial
  xSemaphoreTake(xMutex, xTickToWait);
  Serial.print(intToPrint);
  xSemaphoreGive(xMutex);
}

void breakVentISR(){                                                //ISR function that simulates ventilation breakdown
  ventBroken = !ventBroken;
}
void loop(){}