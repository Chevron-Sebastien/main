// Program for each arduino of the SpaceDog Project from The SPACE by Andrea Ventura
// Each arduino control 1 leg
// The programm is the same for each leg, EXCEPT:
// !!!!!! Set the LEG value acording to the leg you are uploading this program to
// !!!!!! Correct values are 0/1/2/3 for front left/front right/rear left/rear right leg
// !!!!!! The programme then takes care of setting peculiar variables and value specific to each leg 
// !!!!!! Things like minimal angle value (angleZero) or wich PID is reversed due...
// !!!!!! ...to the way the encoders for angle mesurments are mounted are taken care of.

#include <SPI.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <PID_v1.h>
//----------------!!!!!Leg choice!!!!!-------------------
#define LEG 3              // set to 0/1/2/3 accordingly
//----------------output pins----------------------------
#define A_PWM_PIN  5       // Speed of motor A and B in PWM units, value from 0 to 255
#define A_EN_PIN   6       // This sets the direction of motor A and B, set to HIGH for outward motion and LOW for inward motion
// Motor A is top motor B is botom
#define B_PWM_PIN  7
#define B_EN_PIN   8
//                                                                            EN1     EN2     OUT
#define C_PWM_PIN 2        // Motor C is shoulder                              0       0      Brake
#define C_EN1_PIN 3        // Different driver than A and B, 2 enable pins     0       1      Forward
#define C_EN2_PIN 4        // Follow this table for direction:                 1       0      Reverse
// (C_EN1 & 2 are reversed compared to datasheet)                              1       1      Float

//----------------Encoders / potentiometer-----------------------
#define A_CS_PIN 48 //Chip or Slave select Angle A
#define B_CS_PIN 46 //Chip or Slave select Angle B

#define C_POT_PIN A0  // feedback for shoulder

//-----------------------------Direction setting---------------------
//To reverse or not the PID

//----------------Angles Mesurments and calculation variables--------

uint8_t temp[2]; // contains the MSB, LSB of the position

uint16_t ABSposition = 0;  //  stores the Absolute position to be read from the encoder

int motorPwmPinsArray[3] = {A_PWM_PIN, B_PWM_PIN, C_PWM_PIN};
int motorEnablePinsArray[2] = {A_EN_PIN, B_EN_PIN};
int encoderCSPinsArray[2] = {A_CS_PIN, B_CS_PIN};

unsigned long Dt = 0;
unsigned long Mt = 0;

bool flagZero = 0;


double legsAngles[2] = {0, 0};   // A, B
double shoulderPosition = 0; // feedback from shoulder potentiometer

double legsAnglesPrevious[2] = {0, 0};    // memory of previous angle value mesured
//double shoulderPreviousPosition = 0;

double legsAnglesZero[2] = {0, 0};
double shoulderZeroPosition = 0;

double anglesSetpointsArray[2] = {0, 0};
double shoulderSetpoint = 0;

double pwmValue[3] = {0, 0, 0};
int encoderNtours[2] = {0, 0};
int tryErrorAngle[2] = {0, 0};

int kP = 100;
int kP_Shoulder = 100;
//--------------------PID Lib variables--------------------------

#if LEG==0 or LEG==3
PID myPIDmotorA(&legsAngles[0], &pwmValue[0], &anglesSetpointsArray[0], kP, 0, 0, DIRECT);  //direction is changed later...
PID myPIDmotorB(&legsAngles[1], &pwmValue[1], &anglesSetpointsArray[1], kP, 0, 0, REVERSE);  //...according to leg value
#else
PID myPIDmotorA(&legsAngles[0], &pwmValue[0], &anglesSetpointsArray[0], kP, 0, 0, REVERSE);  //direction is changed later...
PID myPIDmotorB(&legsAngles[1], &pwmValue[1], &anglesSetpointsArray[1], kP, 0, 0, DIRECT);  //...according to leg value
#endif

PID myPIDmotorC(&shoulderPosition, &pwmValue[2], &shoulderSetpoint, kP_Shoulder, 0, 0, DIRECT);

//---------------------------------------------------------------
//---------------data and transmissions variables----------------
int val1, val2;
double val3;

const byte numChars = 32;
char receivedChars[numChars];
char tempChars[numChars];        // temporary array for use by strtok() function

// variables to hold the parsed data
char messageFromPC[numChars] = {0};

boolean newData = false;
//---------------------------------------------------------------

void setup()
{
  // put your setup code here, to run once:
  pinMode(A_CS_PIN, OUTPUT); //Slaves encoders configuration
  pinMode(B_CS_PIN, OUTPUT);

  pinMode(A_PWM_PIN, OUTPUT);
  pinMode(A_EN_PIN, OUTPUT);
  pinMode(B_PWM_PIN, OUTPUT);
  pinMode(B_EN_PIN, OUTPUT);

  pinMode(C_PWM_PIN, OUTPUT);
  pinMode(C_EN1_PIN, OUTPUT);
  pinMode(C_EN2_PIN, OUTPUT);

  digitalWrite(A_CS_PIN, HIGH); //Slaves encoders de-selected
  digitalWrite(B_CS_PIN, HIGH);



  //------initialize Timer1 for measure and Timer4 for PID--------
  //visit http://www.engblaze.com/microcontroller-tutorial-avr-and-arduino-timer-interrupts/
  //for a great little tutorial about timer interrupts
  //most of the basic interupt routine used here comes from there

  cli();  // disable global interrupts

  TCCR1A = 0; // set entire TCCR1A register to 0
  TCCR1B = 0; // same for TCCR1B

  TCNT1 = 2500;            //offset the timer by 10ms
  // set compare match register to desired timer count:
  OCR1A = 5000;            //5000 for 20ms timer with 64 prescaler
  // turn on CTC mode:
  TCCR1B |= (1 << WGM12);
  // Set CS10 and 11 bit for 64 prescaler:
  TCCR1B |= (1 << CS10);
  TCCR1B |= (1 << CS11);
  // enable timer compare interrupt:
  TIMSK1 |= (1 << OCIE1A);

  //------------------------------------------------------------------

  TCCR4A = 0; // set entire TCCR4A register to 0
  TCCR4B = 0; // same for TCCR4B

  // set compare match register to desired timer count:
  OCR4A = 5000;            //5000 for 20ms timer with 64 prescaler
  // turn on CTC mode:
  TCCR4B |= (1 << WGM42);
  // Set CS40 and 41 bit for 64 prescaler:
  TCCR4B |= (1 << CS40);
  TCCR4B |= (1 << CS41);
  // enable timer compare interrupt:
  TIMSK4 |= (1 << OCIE4A);

  sei();  // enable global interrupts


  //--------------------End initialize Timer4--------------------------
  //----------Initialize SPI comm for encoders mesurments--------------
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setClockDivider(SPI_CLOCK_DIV64);
  SPI.end();
  //---------------------------End SPI---------------------------------
  //-----------------------PID Library setup---------------------------
  myPIDmotorA.SetOutputLimits(-255, 255);
  myPIDmotorA.SetSampleTime(0);
  myPIDmotorB.SetOutputLimits(-255, 255);
  myPIDmotorB.SetSampleTime(0);
  myPIDmotorC.SetOutputLimits(-255, 255);
  myPIDmotorC.SetSampleTime(0);
  //-------------------------------------------------------------------
  Serial.begin(9600);
  Serial.flush();
}

ISR(TIMER1_COMPA_vect)  //function executed when timer3 interrupt
{ //Angular position mesurments
  getPosition(0);
  getPosition(1);
  getShoulderPosition();
}

ISR(TIMER4_COMPA_vect)  //function executed when timer4 interrupt
{ //PID computation
  //Serial.println("hello3");
  myPIDmotorA.Compute();
  moveMotor(0);
  myPIDmotorB.Compute();
  moveMotor(1);
  myPIDmotorC.Compute();
  moveShoulder(2);
}



double mapfloat(double x, double in_min, double in_max, double out_min, double out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint8_t SPI_T (int index, uint8_t msg)    //Repetive SPI transmit sequence
{
  uint8_t msg_temp = 0;  //  variable to hold received data
  digitalWrite(encoderCSPinsArray[index], LOW); //  select spi device
  msg_temp = SPI.transfer(msg); // sends (receive) message to (from) the encoder #index
  digitalWrite(encoderCSPinsArray[index], HIGH); //  deselect spi device
  delayMicroseconds(5);   //delay here to prevent the AMT20 from having to prioritize SPI over obtaining our position
  return (msg_temp);    //return received byte
}

void getPosition(int index) {
  int tryAmount = 0;
  uint8_t received = 0xA5;    //just a temp variable that the encoder will send if he's not ready of there isn't any remaining data to be sent
  uint16_t ABSposition = 0;  //  stores the Absolute position to be read from the encoder

  SPI.begin();    //start transmition

  SPI_T(index, 0x10);  //issue read command to encoder #1
  received = SPI_T(index, 0x00);   //issue NOP to check if encoder is ready to send
  while (received != 0x10 and tryAmount++ < 5)    //loop while encoder is not ready to send
  {
    received = SPI_T(index, 0x00);   //check again if both encoders are still working
  }

  temp[0] = SPI_T(index, 0x00);   //Receive MSB
  temp[1] = SPI_T(index, 0x00);   //Receive LSB
  temp[0] &= ~ 0xF0;   //mask out the first 4 bits

  SPI.end();    //end transmition

  ABSposition = temp[0] << 8;    //shift MSB to correct ABSposition in ABSposition message
  ABSposition += temp[1];    // add LSB to ABSposition message to complete message

  legsAngles[index] = (ABSposition * 0.08789) + encoderNtours[index] * 360; // approx 360/4096
  //--------------Angle Roll-Over Managements-----------
  if (legsAnglesPrevious[index] - (encoderNtours[index] * 360) > 355 and legsAngles[index] - (encoderNtours[index] * 360) < 5) {
    legsAngles[index] += 360;
    encoderNtours[index] += 1;
  }
  else if (legsAnglesPrevious[index] - (encoderNtours[index] * 360) < 5 and legsAngles[index] - (encoderNtours[index] * 360) > 355) {
    legsAngles[index] -= 360;
    encoderNtours[index] -= 1;
  }
  legsAnglesPrevious[index] = legsAngles[index];
}

void MGD_calculation(){          //Calculating foot (end effector) position based on angle value
  const double len_a = 443.77;   //Femur lenght (mm)
  const double len_b = 424.62;   //Tibia lenght (mm)
  const double a_angle_offset = 53.38;    //Real minimal angle between femur and horizontal (trunk of the robot) in degrees
  const double b_angle_offset = 73.10;    //Real minimal angle between tibia and horizontal (trunk of the robot) in degrees
  double a_delta, b_delta;       //Diff between angles measured and min angles (software values)
  double a_angle, b_angle;       //Calculated real angle value
  double a1, a2;                 //Femur lenght projected 1:vertically (Z axis), 2:horizontally (X axis)
  double b1, b2;                 //Tibia lenght projected 1:vertically (Z axis), 2:horizontally (X axis)
  double pos_x, pos_z;           //X and Z axis position of robot foot (end effector)
  
  a_delta = legsAngles[0]-legsAnglesZero[0];
  b_delta = legsAngles[1]-legsAnglesZero[1];
  
  if (LEG == 0 or LEG == 3){                 //either a or b angle measurment encoder mounted backward...
    b_delta = -b_delta;                      //...depending on leg, need to adapt calculation acordingly
  }
  else{
    a_delta = -a_delta;
  }
  
  a_angle = a_delta+a_angle_offset;
  a1 = sin(a_angle*PI/180)*len_a;
  a2 = cos(a_angle*PI/180)*len_a;

  b_angle = b_delta+b_angle_offset;
  b1 = sin((b_angle-a_angle)*PI/180)*len_b;
  b2 = cos((b_angle-a_angle)*PI/180)*len_b;

  pos_x = -a2+b2;
  pos_z = a1+b1;

  Serial.println(":("+String(pos_x)+","+String(pos_z)+");");
}

void getShoulderPosition() {
  shoulderPosition = analogRead(C_POT_PIN);
}

void IncrSetPoint(int choiceMotor, double choiceAngle) {
  if (flagZero) {
    anglesSetpointsArray[choiceMotor] += choiceAngle;
  }
}

void abslSetPoint(int choiceMotor, double choiceAngle) {
  if (flagZero) {
    anglesSetpointsArray[choiceMotor] = legsAnglesZero[choiceMotor] + choiceAngle;
  }
}

void abslShoulderSetPoint(double choicePosition) {
  if (flagZero) {
    shoulderSetpoint = shoulderZeroPosition + choicePosition;
  }
}

void setZero() {
  for (int i = 0; i <= 1; i++) {
    getPosition(i);
    legsAnglesZero[i] = legsAngles[i];
    anglesSetpointsArray[i] = legsAngles[i];

    Serial.println(":zero set for "+String(i)+" -> "+String(legsAnglesZero[i])+";");
  }
  getShoulderPosition();
  shoulderZeroPosition = shoulderPosition;
  shoulderSetpoint = shoulderPosition;
  flagZero = 1;
  myPIDmotorA.SetMode(AUTOMATIC);
  myPIDmotorB.SetMode(AUTOMATIC);
  myPIDmotorC.SetMode(AUTOMATIC);
}

void moveMotor(int index) {   //apply the pwm calculated by the PID to the motor
  if (pwmValue[index] > 165) {
    digitalWrite(motorEnablePinsArray[index], 1);
    analogWrite(motorPwmPinsArray[index], pwmValue[index]);
  }
  else if (pwmValue[index] < -165) {
    digitalWrite(motorEnablePinsArray[index], 0);
    analogWrite(motorPwmPinsArray[index], -pwmValue[index]);
  }
  else {
    analogWrite(motorPwmPinsArray[index], 0);
  }
}

void moveShoulder(int index) {
  if (pwmValue[index] > 165) {
    digitalWrite(C_EN1_PIN, LOW);
    digitalWrite(C_EN2_PIN, HIGH);
    analogWrite(motorPwmPinsArray[index], pwmValue[index]);
  }
  else if (pwmValue[index] < -165) {
    digitalWrite(C_EN1_PIN, HIGH);
    digitalWrite(C_EN2_PIN, LOW);
    analogWrite(motorPwmPinsArray[index], -pwmValue[index]);
  }
  else {
    digitalWrite(C_EN1_PIN, 0);
    digitalWrite(C_EN2_PIN, 0);
    analogWrite(motorPwmPinsArray[index], 0);
  }
}

void recvWithStartEndMarkers() {
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  if(Serial.available() > 0) {
    rc = Serial.read();
    if (rc == endMarker) {
      recvInProgress = false;
      newData = true;
      receivedChars[ndx] = 0;
      parseData();
    }
    
    if(recvInProgress) {
      receivedChars[ndx] = rc;
      ndx ++;
      if (ndx == numChars) {
        ndx = numChars - 1;
      }
    }

    if (rc == startMarker) { 
      ndx = 0; 
      recvInProgress = true;
    }
  }
}

void parseData() {
  // split the data into its parts
  char * strtokIndx; // this is used by strtok() as an index

  strtokIndx = strtok(tempChars, ",");
  val1 = atoi(strtokIndx);             // convert this part to an integer,
  //can be to a float too with atof?
  strtokIndx = strtok(NULL, ",");
  val2 = atoi(strtokIndx);

  strtokIndx = strtok(NULL, ",");
  val3 = atof(strtokIndx);             // convert this part to a float
}

void interpretData() {
  transmission(0);
  if (val1 == 0) {
    setZero();
  }
  else if (val1 == 1) {
    IncrSetPoint(val2, val3);
  }
  else if (val1 == 2) {
    if (val2 < 2) {
      abslSetPoint(val2, val3);
    }
    else if (val2 == 2) {
      abslShoulderSetPoint(val3);
    }
  }
  else if (val1 == 3) {
    transmission(1);
  }
  else if (val1 == 4) {
    MGD_calculation();
  }
}

void reception() {
  recvWithStartEndMarkers();
  if (newData == true) {
    strcpy(tempChars, receivedChars); //copy to protect original data
    parseData();
    interpretData();
    newData = false;
  }
}

void transmission(int flag) {
  char  buff[40]="";
  if (flag == 0) {
    char  s0[20];
    dtostrf(val3, 4, 2, s0);
    sprintf(buff, ":ack <%d,%d,%s>;", val1, val2, s0);
    Serial.flush();
    Serial.println(buff);
  }
  else if (flag == 1) {
    char  s0[10];
    char  s1[10];
    Serial.println("hello");
    dtostrf(legsAngles[0], 4, 2, s0);
    dtostrf(legsAngles[1], 4, 2, s1);
    sprintf(buff, ":pos is %s,%s;", s0, s1);
    Serial.flush();
    Serial.println(buff);
  }
}

void loop() {
  reception();
}
