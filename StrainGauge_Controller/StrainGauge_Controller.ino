//Controller Arduino for Strain Gauge measurements

//Author: David P. Reid

//Communicates through I2C with PERIPHERAL Arduino to receive strain gauge measurements.
#include "TrussStepper.h"
#include "ArduinoJson-v6.9.1.h"
#include <Wire.h>

const uint32_t I2C_BAUD_RATE = 400000; //100000 -> default; 400000 -> fastmode

//JSON serialization
#define COMMAND_SIZE 64  //originally 64
StaticJsonDocument<COMMAND_SIZE> doc;
char command[COMMAND_SIZE];

//STEPPER VARIABLES
#define SEN 4
#define SDIR 2
#define SPUL 3
const int stepperStepsPerRev = 200;
const int stepperStepPeriod = 1000; //microseconds
TrussStepper stepper = TrussStepper(stepperStepsPerRev, SDIR, SPUL, SEN);
int currentPos = 0;     //the position of the stepper in terms of number of steps
int moveToPos = 0;      //the position the stepper should move to in terms of steps.
const int positionLimit = 100*stepperStepsPerRev;
const int direction = -1;   //reverse the direction of the steps -> -1
bool isStepperEnabled = false;

//LIMIT SWITCHES
bool limitSwitchesAttached = false;
#define limitSwitchLower 11
bool lowerLimitReached = false;
#define limitSwitchUpper 9
bool upperLimitReached = false;

//TIMING FOR GAUGE READING
unsigned long timeInterval = 1000;    //request gauge readings on this time interval
unsigned long currentTime = millis();

//GAUGE VARIABLES
const int PERIPHERAL_ADDRESS = 8;     //I think 0-7 are reserved for other things?
const int numGauges = 7;
int next_index = 0;       //the index of the next gauge to read data from

//LEDs - rotation indicator
#define LED_UP 13
#define LED_DOWN 14


typedef union
{
  float number;
  uint8_t bytes[4];
} FLOATUNION;

FLOATUNION data_0;
FLOATUNION data_1;
FLOATUNION data_2;
FLOATUNION data_3;
FLOATUNION data_4;
FLOATUNION data_5;
FLOATUNION data_6;

FLOATUNION data[numGauges] = {data_0, data_1, data_2, data_3, data_4, data_5, data_6};

/**
 * Defines the valid states for the state machine
 * 
 */
typedef enum
{
  STATE_STANDBY = 0,        //no drive to motor, no reading of gauges
  STATE_READ = 1,           //requests, reads data from peripheral and writes data to serial
  STATE_MOVE = 2,           //allows stepper motor to move to new position
  STATE_ZERO = 3,           //zeroes the position of the servo
  STATE_TARE = 4,           //tares (zeroes) the gauge readings
  STATE_TARE_LOAD = 5,
  STATE_GAUGE_RESET = 6,
  
} StateType;

//state Machine function prototypes
//these are the functions that run whilst in each respective state.
void Sm_State_Standby(void);
void Sm_State_Read(void);
void Sm_State_Move(void);
void Sm_State_Zero(void);
void Sm_State_Tare(void);
void Sm_State_Tare_Load(void);
void Sm_State_Gauge_Reset(void);

/**
 * Type definition used to define the state
 */
 
typedef struct
{
  StateType State; /**< Defines the command */
  void (*func)(void); /**< Defines the function to run */
} StateMachineType;

/**
 * A table that defines the valid states of the state machine and
 * the function that should be executed for each state
 */
StateMachineType StateMachine[] =
{
  {STATE_STANDBY, Sm_State_Standby},
  {STATE_READ, Sm_State_Read},
  {STATE_MOVE, Sm_State_Move},
  {STATE_ZERO, Sm_State_Zero},
  {STATE_TARE, Sm_State_Tare},
  {STATE_TARE_LOAD, Sm_State_Tare_Load},
  {STATE_GAUGE_RESET, Sm_State_Gauge_Reset},
};
 
int NUM_STATES = 7;

/**
 * Stores the current state of the state machine
 */
 
StateType SmState = STATE_READ;    //START IN THE READ STATE


//DEFINE STATE MACHINE FUNCTIONS================================================================

//TRANSITION: STATE_STANDBY -> STATE_STANDBY
void Sm_State_Standby(void){

  if(isStepperEnabled)
  {
    stepper.disable();
    isStepperEnabled = false;
  }
  
  if(limitSwitchesAttached)
  {
    detachInterrupt(digitalPinToInterrupt(limitSwitchLower));
    detachInterrupt(digitalPinToInterrupt(limitSwitchUpper));

    limitSwitchesAttached = false;
  }

  
  SmState = STATE_STANDBY;
}

//TRANSITION: STATE_READ -> STATE_READ
void Sm_State_Read(void){

  upperLimitReached = false;    //if in read state then clear the limit flags.    =====NEW
  lowerLimitReached = false;
  
  if(isStepperEnabled)
  {
    stepper.disable();
    isStepperEnabled = false;
  }
  
   bool error = false;
  
  if(millis() - currentTime >= timeInterval)
  {
    char g[1];
    sprintf(g, "%d", next_index);
    Wire.beginTransmission(PERIPHERAL_ADDRESS);
    Wire.write(g);
    if(Wire.endTransmission() == 0)
    {
      //success
      error = false;
    }
    else
    {
      //failed to send to peripheral
      error = true;
    }
    
    delay(100);   //necessary?

    if(Wire.requestFrom(PERIPHERAL_ADDRESS, 4))
    //if(Wire.available())   //request 4 bytes of data from each gauge (returning a float value) from peripheral address PERIPHERAL_ADDRESS
    {     
      
      for(byte i=0; i<4; i++)
      {
          data[next_index].bytes[i] = Wire.read();
      }
      
      next_index = (next_index + 1) % numGauges;
        
    } 
    else 
    {
      error = true;
      Serial.print("{\"error\":\"gauge\":");
      Serial.print(next_index);
      Serial.println("}");
      //if there is an error in a gauge reading then reset to 0 index on controller and peripheral.
//      Wire.beginTransmission(PERIPHERAL_ADDRESS);
//      Wire.write('0');
//      Wire.endTransmission();
      next_index = (next_index + 1) % numGauges;
      
      
//      next_index = 0;
    }

    report();
    currentTime = millis();

  }

  if(error)
  {
    Wire.begin();     //error in wire so start again
    Wire.setClock(I2C_BAUD_RATE);
    error = false;
    SmState = STATE_READ;
    
  } 
  else 
  {
    SmState = STATE_READ;
  }
  
}

//TRANSITION: STATE_MOVE -> STATE_READ
//Remains in move state until current position matches moveTo position.
//This blocks gauge reading, but high stepper speed and slow update of gauges should make this fine.
void Sm_State_Move(void){

  bool up = true;
  
  if(!isStepperEnabled)
  {
    stepper.enable();
    isStepperEnabled = true;
  }
  
  if(!limitSwitchesAttached)
  {
    attachInterrupt(digitalPinToInterrupt(limitSwitchLower), doLimitLower, FALLING);
    attachInterrupt(digitalPinToInterrupt(limitSwitchUpper), doLimitUpper, FALLING);

    limitSwitchesAttached = true;
  }

  if(lowerLimitReached || upperLimitReached)
  {
    lowerLimitReached = false;
    upperLimitReached = false;
  }
  
  if(moveToPos != currentPos)
  {
    if(currentPos > moveToPos)
    {
      //step clockwise with stepper class
      stepper.step(-1*direction);    //might want to put a direction offset in the library
      currentPos -= 1;
      up = false;
    } 
    else if(currentPos < moveToPos)
    {
      //step anticlockwise with stepper class
      stepper.step(1*direction);
      currentPos += 1;
      up = true;  
    }

    enableRotationLEDs(up);
    SmState = STATE_MOVE;
    
  }
  else
  {
    //current position has reached the requested moveTo position so can go back to reading the gauges.
    disableRotationLEDs();
    SmState = STATE_READ;
  }
  
}

//TRANSITION: STATE_ZERO -> STATE_READ
//Move to the upper limit switch and then makes a fixed number of steps downwards and sets this as 0 position.
void Sm_State_Zero(void){

  if(!limitSwitchesAttached)
  {
    attachInterrupt(digitalPinToInterrupt(limitSwitchLower), doLimitLower, FALLING);
    attachInterrupt(digitalPinToInterrupt(limitSwitchUpper), doLimitUpper, FALLING);

    limitSwitchesAttached = true;
  }
  
  if(!isStepperEnabled)
  {
    stepper.enable();
    isStepperEnabled = true;
  }
  
  if(!upperLimitReached)
  {
    stepper.step(-1*direction);
    currentPos -= 1;      //not necessary?
    SmState = STATE_ZERO;
  }
  else 
  {
    SmState = STATE_READ;
  }
 
}

//TRANSITION: STATE_TARE -> STATE_READ
void Sm_State_Tare(void){

  if(isStepperEnabled)
  {
    stepper.disable();
    isStepperEnabled = false;
  }
  
  Wire.beginTransmission(PERIPHERAL_ADDRESS);
  Wire.write('t');
  Wire.endTransmission();
  delay(2000);
  
  SmState = STATE_READ;
  
}

//TRANSITION: STATE_TARE -> STATE_READ
void Sm_State_Tare_Load(void){

  if(isStepperEnabled)
  {
    stepper.disable();
    isStepperEnabled = false;
  }
  
  Wire.beginTransmission(PERIPHERAL_ADDRESS);
  Wire.write('l');
  Wire.endTransmission();
  delay(2000);
  
  SmState = STATE_READ;
  
}

//TRANSITION: STATE_GAUGE_RESET -> STATE_READ
void Sm_State_Gauge_Reset(void){

  if(isStepperEnabled)
  {
    stepper.disable();
    isStepperEnabled = false;
  }
  
  Wire.beginTransmission(PERIPHERAL_ADDRESS);
  Wire.write('r');
  Wire.endTransmission();
  delay(100);
  
  SmState = STATE_READ;
  
}

//STATE MACHINE RUN FUNCTION
void Sm_Run(void)
{
  if (SmState < NUM_STATES)
  {
    SmState = readSerialJSON(SmState);      
    (*StateMachine[SmState].func)();        //reads the current state and then runs the associated function
    
  }
  else{
    Serial.println("Exception in State Machine");
  }
  
}

void setup() {

  pinMode(limitSwitchLower, INPUT_PULLUP);
  pinMode(limitSwitchUpper, INPUT_PULLUP);
  pinMode(LED_UP, OUTPUT);
  pinMode(LED_DOWN, OUTPUT);
  digitalWrite(LED_UP, LOW);
  digitalWrite(LED_DOWN, LOW);
  
  //I2C communication with peripheral arduino
  Wire.begin();
  Wire.setClock(I2C_BAUD_RATE); 

  //Serial communication for sending data -> RPi -> Server
  Serial.begin(57600);
  while(!Serial);

  stepper.setDelay(stepperStepPeriod);
  stepper.disable();
  isStepperEnabled = false;

  //on startup make sure peripheral device has set the gauge index to 0.
//  Wire.beginTransmission(PERIPHERAL_ADDRESS);
//  Wire.write('0');
//  Wire.endTransmission();
 }

void loop() {
  
  Sm_Run();  

}

StateType readSerialJSON(StateType SmState){
  if(Serial.available() > 0)
  {

    Serial.readBytesUntil(10, command, COMMAND_SIZE);
    deserializeJson(doc, command);
    
    const char* set = doc["set"];

    if(strcmp(set, "speed")==0)     //no implementation of speed change yet....
    {
      float new_speed = doc["to"];
    } 
    else if(strcmp(set, "position")==0)
    {
  
        float new_position = doc["to"];
        
        if(new_position >= -positionLimit && new_position <= positionLimit)
        {
          moveToPos = new_position;
        } 
        else
        {
          Serial.println("Outside position range");
        }
     
  } 
    else if(strcmp(set, "mode")==0)
    {
      
      const char* new_mode = doc["to"];

        if(strcmp(new_mode, "standby") == 0)
        {
          SmState = STATE_STANDBY;
          reportState(STATE_STANDBY);
        } 
        else if(strcmp(new_mode, "read") == 0)
        {
          SmState = STATE_READ;
        }
        else if(strcmp(new_mode, "move") == 0)
        {
          SmState = STATE_MOVE;
          reportState(STATE_MOVE);
        }
        else if(strcmp(new_mode, "zero") == 0)
        {
          SmState = STATE_ZERO;
          reportState(STATE_ZERO);
        }
        else if(strcmp(new_mode, "tare") == 0)
        {
          SmState = STATE_TARE;
          reportState(STATE_TARE);
        }
        else if(strcmp(new_mode, "tare_load") == 0)
        {
          SmState = STATE_TARE_LOAD;
          reportState(STATE_TARE_LOAD);
        }
        else if(strcmp(new_mode, "gauge_reset") == 0)
        {
          SmState = STATE_GAUGE_RESET;
          reportState(STATE_GAUGE_RESET);
        }
        
    }  
    
  }
      return SmState;     //return whatever state it changed to or maintain the state.
 } 

//On an interrupt - will interrupt all state functions
//TRANSITION: -> READ
void doLimitLower(void){
  if(!lowerLimitReached)
  {
    //if the lower limit is reached, then the stepper should move a small distance back towards the centre, away from the limit
    lowerLimitReached = true;
    
    //TEMP OUTPUT OF DATA
    Serial.print("Lower limit at pos: ");
    Serial.println(currentPos);
      
    stepper.step(-15*stepperStepsPerRev*direction);
    moveToPos = currentPos;
    SmState = STATE_READ; 
  }
  
    

}

//On an interrupt - will interrupt all state functions
//TRANSITION: -> READ
void doLimitUpper(void){
  if(!upperLimitReached)
  {
    upperLimitReached = true;

    //TEMP OUTPUT OF DATA
    //Serial.print("Upper limit at pos: ");
    //Serial.println(currentPos);

    stepper.step(15*stepperStepsPerRev*direction);
    currentPos = 0;
    moveToPos = 0;
    SmState = STATE_READ;  
  }
    
    

}

void report(){
  Serial.print("{\"load_cell\":");
  Serial.print(data[0].number);
  
  for(int i=1;i<numGauges;i++){
    Serial.print(",\"gauge_");
    Serial.print(i);
    Serial.print("\":");
    Serial.print(data[i].number);
  }

  Serial.print(",\"state\":");
  Serial.print(SmState);
  Serial.print(",\"pos\":");
  Serial.print(currentPos);
  
  Serial.println("}");
 
}

void reportState(int state){
  Serial.print("{\"state\":");
  Serial.print(state);
  Serial.println("}");
}

void enableRotationLEDs(bool up) {
  if(up){
    digitalWrite(LED_UP, HIGH);
    digitalWrite(LED_DOWN, LOW);
  } else{
    digitalWrite(LED_UP, LOW);
    digitalWrite(LED_DOWN, HIGH);
  }
}

void disableRotationLEDs() {
  digitalWrite(LED_UP, LOW);
    digitalWrite(LED_DOWN, LOW);
}
