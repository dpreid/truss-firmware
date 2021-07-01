//v0.1
//PERIPHERAL Arduino for taking parallel strain gauge measurements.
//Each (full bridge) Wheatstone bridge is connected to an HX711 analogue to digital converter (ADC).
//Each HX711 has a DATA (DT) pin connected to a unique pin on the Arduino.
//Each HX711 is also connected to a common CLOCK (SCK) pin.
//Vcc is Arduino logic level (+3.3V for Nano).

//Measure and transfer strain gauge values through I2C when requested from the CONTROLLER

#include "HX711.h"
#include <Wire.h>

const int numGauges = 3;
const int GAUGE_0_DT = 3; //DATA pins
const int GAUGE_1_DT = 4;
const int GAUGE_2_DT = 5;
//const int GAUGE_3_DT = 6;
//const int GAUGE_4_DT = 7;
//const int GAUGE_5_DT = 8;
//const int GAUGE_6_DT = 9;

//const int data_pins[numGauges] = {GAUGE_0_DT, GAUGE_1_DT, GAUGE_2_DT, GAUGE_3_DT, GAUGE_4_DT, GAUGE_5_DT, GAUGE_6_DT};
const int data_pins[numGauges] = {GAUGE_0_DT, GAUGE_1_DT, GAUGE_2_DT};
const int SCK_PIN = 2;  //Common CLOCK pin

HX711 scale_0;
HX711 scale_1;
HX711 scale_2;
//HX711 scale_3;
//HX711 scale_4;
//HX711 scale_5;
//HX711 scale_6;

//HX711 gaugeScales[numGauges] = {scale_0, scale_1, scale_2, scale_3, scale_4, scale_5, scale_6};
HX711 gaugeScales[numGauges] = {scale_0, scale_1, scale_2};
typedef union
{
  float number;
  uint8_t bytes[4];
} FLOATUNION;

FLOATUNION data[numGauges];



void setup() {
 initialiseScales();
 setGain(128);
 
 gaugeScales[0].set_scale(412);
 //gaugeScales[0].set_scale(-15184);   //calibrated with the load cell on the real truss -> OUTPUTS force in newtons
 //gaugeScales[1].set_scale(-3231);         //calibrated with truss member 1  -> outputs strain in micro-strain
 
  gaugeScales[1].set_scale(-3900);          //member 1
  gaugeScales[2].set_scale(-3900);          //member 2
//  gaugeScales[3].set_scale(-3900);          //member 3
//  gaugeScales[4].set_scale(-3900);          //member 4
//  gaugeScales[5].set_scale(-3900);          //member 5
//  gaugeScales[6].set_scale(-3900);          //member 6

  
 tareAllScales();

  //I2C communication with CONTROLLER arduino
 Wire.begin(8);
 Wire.onRequest(requestHandler);
 Wire.onReceive(receiveHandler);

 Serial.begin(57600);
 while(!Serial);
  
}

void loop() {
  
//  for(int i=0; i<numGauges;i++){
//    if(gaugeScales[i].is_ready()){
//      Serial.print("Gauge ");
//      Serial.print(i);
//      Serial.print(": \t");
//      Serial.println(gaugeScales[i].get_units(10));
//    } else{
//      Serial.println("Not ready");
//    }
//
//    delay(100);    
//  }

//  if(scale_0.is_ready()){
//    Serial.print("Gauge 0: ");
//    Serial.println(scale_0.get_units(10));
//  } else{
//    Serial.println("Not ready");
//  }
//  
//  Serial.println("Waiting..");
//  delay(1000);
}

void requestHandler(){
  
  for(int i=0; i<numGauges;i++){
    
    FLOATUNION d;
    
    if(gaugeScales[i].is_ready()){
      
      d.number = gaugeScales[i].get_units(10);
      data[i] = d;
      
    } else{
      
      d.number = 1.0;
      data[i] = d;
    }

    delay(100);
  }

  
  for(int i=0;i<numGauges;i++){
    
    Wire.write(data[i].bytes, 4);
  
  }
  
}

void receiveHandler(int numBytes){
  char c = Wire.read();
  if(c == 't'){
    tareAllScales();
  }
}

void initialiseScales(){
  for(int i=0;i<numGauges;i++){
    gaugeScales[i].begin(data_pins[i], SCK_PIN);
  }
}

void setGain(int gain){
  for(int i=0;i<numGauges;i++){
    gaugeScales[i].set_gain(gain);
  }
}

void tareAllScales(){
  for(int i=0;i<numGauges;i++){
    gaugeScales[i].tare();
   }
}
