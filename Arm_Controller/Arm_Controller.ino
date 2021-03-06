/*
 * Organization   : Boston University UAV Team
 * Authors        : Nick Brisco, Connor McCann
 * Event          : Control Theory Event
 * Date           : 31 Mar 2017
 * 
 * This code will supprt the testing rig we built for the control 
 * theory event helod on 32 Mar 2017 with Kenneth Sebesta.
 * 
 * The purpose of this demo is to introduce UAV members to the use of PID
 * control loops to stabalize a system. In this case, we have a DC motor 
 * at the end of a vehicel arm attached to a pivot point which is clamped 
 * to the table. 
 * 
 * Each team should be able to use the data from the onboard IMU to set the angle 
 * of the arm to be +- ~30 deg from level using an arduino and the required sensors
 * 
 * Equipment: 
 *  - Test Rig (Designed by Andrew Lee)
 *  - DC motor
 *  - ESC
 *  - IMU
 *  - Arduino Uno 
 *  
 */
#include <PID_v1.h>
#include <Servo.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

#define OUTPUT_READABLE_YAWPITCHROLL
#define ESC_PIN     10  // Where the ESC is connected (PWM)
#define TESTING     0
#define INTERRUPT_PIN 2  // use pin 2 on Arduino Uno & most boards

//==========================================================================
//                          MPU6050 Global Vars
//==========================================================================
// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// packet structure for InvenSense teapot demo
uint8_t teapotPacket[14] = { '$', 0x02, 0,0, 0,0, 0,0, 0,0, 0x00, 0x00, '\r', '\n' };

MPU6050 mpu;
//==========================================================================
//                    INTERRUPT DETECTION ROUTINE                
//==========================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
            mpuInterrupt = true;
}

//==========================================================================
//                          Servo / PID           
//==========================================================================
Servo ESC; // Create the ESC Servo

double setpoint = 0;
double Input; // input is form IMU
double Output; // output is going to the ESC

double Kp=0, Ki=0, Kd=0; //5,5,.12
PID motor_PID(&Input, &Output, &setpoint, Kp, Ki, Kd, DIRECT); // set the characteristics of the controller

char command = '\0'; // null value for default command
double oldTime;
int program_run = 0;

//==========================================================================
//                      Forward Declarations          
//==========================================================================
void get_PIDs(void);

//==========================================================================
//                            SETUP LOOP
//==========================================================================
void setup() {
  Serial.begin(115200); // Serial command line interface
  // MPU6050
  
  // join I2C bus (I2Cdev library doesn't do this automatically)
  #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
      Wire.begin();
      Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
  #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
      Fastwire::setup(400, true);
  #endif

  while (!Serial);
  Serial.println(F("Initializing I2C devices..."));

  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);
  
  // verify connection
  Serial.println(F("Testing device connections..."));
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

  // load and configure the DMP
  Serial.println(F("Initializing DMP..."));
    
//  while (Serial.available() && Serial.read()); // empty buffer
//  while (!Serial.available());                 // wait for data
//  while (Serial.available() && Serial.read()); // empty buffer again
  devStatus = mpu.dmpInitialize();

  // supply your own gyro offsets here, scaled for min sensitivity
  mpu.setXGyroOffset(220);
  mpu.setYGyroOffset(76);
  mpu.setZGyroOffset(-85);
  mpu.setZAccelOffset(1788); // 1688 factory default for my test chip

  // make sure it worked (returns 0 if so)
  if (devStatus == 0) {
      // turn on the DMP, now that it's ready
      Serial.println(F("Enabling DMP..."));
      mpu.setDMPEnabled(true);

      // enable Arduino interrupt detection
      Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
      attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
      mpuIntStatus = mpu.getIntStatus();

      // set our DMP Ready flag so the main loop() function knows it's okay to use it
      Serial.println(F("DMP ready! Waiting for first interrupt..."));
      dmpReady = true;

      // get expected DMP packet size for later comparison
      packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
      // ERROR!
      // 1 = initial memory load failed
      // 2 = DMP configuration updates failed
      // (if it's going to break, usually the code will be 1)
      Serial.print(F("DMP Initialization failed (code "));
      Serial.print(devStatus);
      Serial.println(F(")"));
  }
  
  // Everything Else --------------------------------------------------------------------------
  ESC.attach(ESC_PIN); // Setting up the ESC
  //ESC.writeMicroseconds(2000);
  //delay(3000);
  ESC.writeMicroseconds(1000);
  delay(3000);
  //ESC.writeMicroseconds(2000);
  //delay(3000);
  setpoint; 
  motor_PID.SetMode(AUTOMATIC);
  motor_PID.SetOutputLimits(1100,1600);
  Serial.println("======================================");
  Serial.println("           Your Commands:");
  Serial.println("======================================");
  Serial.println("r: to run");
  Serial.println("q: to stop");
  Serial.println("s <value>: set the P-term");
  Serial.println("k <value>: set the P-term");
  Serial.println("i <value>: set the I-term");
  Serial.println("d <value>: set the D-term");
  Serial.println("--------------------------------------");
}

//==========================================================================
//                             MAIN LOOP
//==========================================================================
void loop() {

  get_PIDs();

  if (program_run) {
    if (TESTING) {
      ESC.writeMicroseconds(1200);
      Serial.println("Testing ESC...");
      delay(100);
    }
    else {
  
      // MPU6050------------------------------------------------------------------
      // if programming failed, don't try to do anything
      if (!dmpReady) return;
  
      // wait for MPU interrupt or extra packet(s) available
      while (!mpuInterrupt && fifoCount < packetSize) {
          // other program behavior stuff here
          // .
          // .
          // .
          // if you are really paranoid you can frequently test in between other
          // stuff to see if mpuInterrupt is true, and if so, "break;" from the
          // while() loop to immediately process the MPU data
          // .
          // .
          // .
      }
  
      // reset interrupt flag and get INT_STATUS byte
      mpuInterrupt = false;
      mpuIntStatus = mpu.getIntStatus();
  
      // get current FIFO count
      fifoCount = mpu.getFIFOCount();
  
      // check for overflow (this should never happen unless our code is too inefficient)
      if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
          // reset so we can continue cleanly
          mpu.resetFIFO();
          Serial.println(F("FIFO overflow!"));
  
      // otherwise, check for DMP data ready interrupt (this should happen frequently)
      } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    
        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        
        // track FIFO count here in case there is > 1 packet available
        // (this lets us immediately read more without waiting for an interrupt)
        fifoCount -= packetSize;
    
        #ifdef OUTPUT_READABLE_QUATERNION
            // display quaternion values in easy matrix form: w x y z
            mpu.dmpGetQuaternion(&q, fifoBuffer);
  //          Serial.print("quat\t");
  //          Serial.print(q.w);
  //          Serial.print("\t");
  //          Serial.print(q.x);
  //          Serial.print("\t");
  //          Serial.print(q.y);
  //          Serial.print("\t");
  //          Serial.println(q.z);
        #endif
    
        #ifdef OUTPUT_READABLE_EULER
            // display Euler angles in degrees
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetEuler(euler, &q);
  //          Serial.print("euler\t");
  //          Serial.print(euler[0] * 180/M_PI);
  //          Serial.print("\t");
  //          Serial.print(euler[1] * 180/M_PI);
  //          Serial.print("\t");
  //          Serial.println(euler[2] * 180/M_PI);
        #endif
    
        #ifdef OUTPUT_READABLE_YAWPITCHROLL
            // display Euler angles in degrees
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
  //          Serial.print("ypr\t");
  //          Serial.print(ypr[0] * 180/M_PI);
  //          Serial.print("\t");
  //          Serial.print(ypr[1] * 180/M_PI);
  //          Serial.print("\t");
  //          Serial.println(ypr[2] * 180/M_PI);
            Input = ypr[2] * 180/M_PI;
           
        #endif
    
        #ifdef OUTPUT_READABLE_REALACCEL
            // display real acceleration, adjusted to remove gravity
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetAccel(&aa, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
  //          Serial.print("areal\t");
  //          Serial.print(aaReal.x);
  //          Serial.print("\t");
  //          Serial.print(aaReal.y);
  //          Serial.print("\t");
  //          Serial.println(aaReal.z);
        #endif
    
        #ifdef OUTPUT_READABLE_WORLDACCEL
            // display initial world-frame acceleration, adjusted to remove gravity
            // and rotated based on known orientation from quaternion
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetAccel(&aa, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
            mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &q);
  //          Serial.print("aworld\t");
  //          Serial.print(aaWorld.x);
  //          Serial.print("\t");
  //          Serial.print(aaWorld.y);
  //          Serial.print("\t");
  //          Serial.println(aaWorld.z);
        #endif
    
        #ifdef OUTPUT_TEAPOT
            // display quaternion values in InvenSense Teapot demo format:
            teapotPacket[2] = fifoBuffer[0];
            teapotPacket[3] = fifoBuffer[1];
            teapotPacket[4] = fifoBuffer[4];
            teapotPacket[5] = fifoBuffer[5];
            teapotPacket[6] = fifoBuffer[8];
            teapotPacket[7] = fifoBuffer[9];
            teapotPacket[8] = fifoBuffer[12];
            teapotPacket[9] = fifoBuffer[13];
            Serial.write(teapotPacket, 14);
            teapotPacket[11]++; // packetCount, loops at 0xFF on purpose
        #endif
  
      motor_PID.Compute(); // Execute PID Controller
      //Serial.print("PID OUTPU------------  ");
      //Serial.print(Output);
      ESC.write(Output);
      //Serial.println(Output);
      }    
    }
  }
}

// ================================================================
//                     Functions Declarations           
// ================================================================
void get_PIDs(void) {
  /* Command Line Format
   *  r - to run the program
   *  k - to terminate the program
   *  p value\n
   *  i value\n
  */

  String input_string = "\0";
  char input_char = '\0';
  String coef = "\0";
  int space = 0;
  int newLine = 0;
  float value = 0;

  // Get the command line date from the user input
  while (Serial.available()) {
    delay(3); // allow the input buffer to fill

    if (Serial.available() > 0) {
      input_char = (char)Serial.read(); // gets one byte from serial buffer
      input_string += input_char; // add it to our running string
    }
  }

  // Parse the command line input string
  if (input_string.length() > 0) {
    
    coef = input_string.substring(0,1);
    
    if (coef.equals("q")) {
      Serial.println("turning off the motor");
      // turn off esc
      program_run = 0;
      ESC.writeMicroseconds(1000);
    }
    else if (coef.equals("r")) {
      program_run = 1;
      Serial.println("Starting Motor");
      ESC.writeMicroseconds(1100);
    }

    // otherwise determine what commands we received
    space = input_string.indexOf(' ');
    newLine = input_string.indexOf('\n');
    value = input_string.substring(space,newLine).toFloat(); // 0 if nothing is passed to it

    // Set the correct coefficient
    if (coef.equals("s")) {
      setpoint = (double)value;
      Serial.print("Setpoint: "); Serial.println(value);
    }
    else if (coef.equals("p")) {
      Kp = value;
      Serial.print("Kp: "); Serial.println(value);
    }
    else if (coef.equals("i")) {
      Ki = value;
      Serial.print("Ki: "); Serial.println(value);
    } 
    else if (coef.equals("d")) {
      Kd = value;
      Serial.print("Kd: "); Serial.println(value);
    }
  }
}
  
