
 //Librairies 
 #include <Encoder.h>
 #include <FastCRC.h>
 #include <PacketSerial.h>
 #include <math.h>
 #include "PulsePin.h"

 //Serial port 
 #define EXTSERIAL Serial1

 //Data packet 
FastCRC16 CRC16;
PacketSerial packetSerialA;
PacketSerial packetSerialB;


 //Definition Analog and Digital pin and States channel
const int pinsAnalogIn[] = {16, 17, 18, 19, 20, 21, 22, 23};
const int nAnalogIn = sizeof(pinsAnalogIn) / sizeof(pinsAnalogIn[0]);
const int pinsDigitalIn[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const int nDigitalIn = sizeof(pinsDigitalIn) / sizeof(pinsDigitalIn[0]);
const int pinsDigitalOut[] = {24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
const int nDigitalOut =sizeof(pinsDigitalOut) / sizeof(pinsDigitalOut[0]);;
const int nStates = 8;

const int pinsDigital[] ={0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
const int nDigital = sizeof(pinsDigital) / sizeof(pinsDigital[0]);

//Definition pin in use on teensy
#define CAMERA1 14
#define CAMERA2 15
#define WHEEL_ENC_PINA 2
#define WHEEL_ENC_PINB 3
#define WHEEL_ENC_SW 4
#define PIN_CAMERA_FSTROBE 12
#define BLICK 5
#define LICK 17
#define REWARD 24
#define VALVE 25
#define LICKDETECT 26
#define PIN_SYNC_LED 38
#define trigger_c 39
//#define BASLER1 30
//#define BASLER2 31 
//#define LED1 32
//#define LED2 33
//#define LED3 34
#define ephys_trigger 36
#define ephys_sync 37
//Pin used for communication
#define GATHER_INDICATOR 41
#define LOOP_INDICATOR 40
#define EXPER 30
#define SHOCK 31
#define PRESHOCK 32
#define TESTSHOCK 35
//LED
const int ledPin = LED_BUILTIN;

//Moving average for lick detection 
const int windowSize = 10;    // size of moving average window
int lickReadings[windowSize]; // the readings from the lick input
int windowIndex = 0;          // the index of the current reading
int totalLickReadings = 0;    // the running total
int averageLickReadings = 0;  // the average
int lickThresh = 2600;
bool binaryLick = false;

//Pulse pin for the pupil camera 
const int pinsPulsePins[] = {30, 31};
const int nPulsePins = sizeof(pinsPulsePins) / sizeof(pinsPulsePins[0]);
PulsePin** pulsePins = new PulsePin*[nPulsePins];

//Timing values 
IntervalTimer gatherTimer;
elapsedMicros current_micros;
elapsedMillis current_millis;

//Encoder
Encoder wheelEncoder(WHEEL_ENC_PINA, WHEEL_ENC_PINB);

//Intermediate values
long encoderPosition = 0;
int last_packet_took = 0;
int reward =0;
int lick = 0;
int lickBaselineCorr;
int lickBaseline;
int ipacket=0;
int frame=0;
int camera_trigger =0;
int ephys=0;
int rad=0;
int exper=0;
int preshock=0;
int epacket=0;
int t=0;
int iPacket=0;
volatile unsigned char counter = 0;
volatile unsigned long packetCount = 0;
volatile long bufferedStates[nStates];

bool gatherNow = false;
bool packetReady = false;

//Definition packet type
enum packetType: uint8_t {
  ptSTATUS,
  ptINSTR,
  ptERROR,
  ptOK,
  ptACK
};

//Synchronization pupil camera 
const uint16_t syncCounterMax = 0x95FF;
const uint16_t syncCounterMin = 0x9500;
volatile uint16_t syncCounter = syncCounterMax;
volatile uint16_t syncCounterFrameInterval = 100;  // count N frames as 'clock'
volatile byte syncCounterIdx = 0;
volatile byte syncCounterSubIdx = 0;
volatile bool updateSyncCounter = true;

//Structure data packet 
struct dataPacket {
    uint8_t type;                   // 1 B, packet type
    uint8_t length;                 // 1 B, packet size
    uint16_t crc16;                 // 2 B, CRC16
    unsigned long packetID;         // 4 B, running packet count
    
    unsigned long us_start;         // 4 B, gather start timestamp
    unsigned long us_end;           // 4 B, transmit timestamp
    uint16_t analog[nAnalogIn];     // 16 B, ADC values
    long variables[nStates];        // 32 B, variables (encoder, speed, etc)
    uint16_t digitalIn;             // 2 B, digital inputs
    uint16_t digitalOut;             // 1 B, digital outputs
    uint8_t padding[1];             // 1 B, align to 4B
    
    dataPacket() : type(ptSTATUS),
                   length(sizeof(dataPacket)),
                   crc16(0),
                   packetID(packetCount++),
                   digitalIn(0),
                   digitalOut(0) {}
};

//Instruction packet
enum instructionType: uint8_t {
  instPIN_LOW     = 0,
  instPIN_HIGH    = 1,
  instPIN_TOGGLE  = 2,
  instPIN_PULSE   = 3,
  instSET_STATE   = 4,
  instUNITY       = 5,
  instHANDSHAKE   = 149,
  instRESET       = 6
};

const uint8_t strideInstLOW = 2;
const uint8_t strideInstHIGH = 2;
const uint8_t strideInstTOGGLE = 2;
const uint8_t strideInstUNITY = 2;
const uint8_t strideInstPULSE = 5;
const uint8_t strideInstSTATE = 5;

union bytesToLong {
  byte bytes[4];
  unsigned long ulong;
  long slong;
};

//Error packet
struct errorPacket {
    uint8_t type;          // 1 B, packet type
    uint8_t length;        // 1 B, packet size
    uint16_t crc16;        // 2 B, CRC16
    unsigned long packetID;// 4 B, running packet count
    unsigned long us_start;// 4 B, gather start timestamp

    char message[16];      // 16 B, error message

    errorPacket() : type(ptERROR),
                   length(sizeof(errorPacket)),
                   crc16(0),
                   packetID(packetCount++) {}    
};

// current state, will be overwritten on gather
dataPacket State;

// Option to reset all the values
void reset() {
  noInterrupts();
  wheelEncoder.write(0);
  current_millis = 0;
  current_micros = 0;

  for (int i=0; i<nStates; i++) {
    bufferedStates[i] = 0;
  }

  for (int i=0; i<nDigitalOut; i++) {
    digitalWriteFast(pinsDigitalOut[i], LOW);
  }
    for (int i=0; i<nDigitalIn; i++) {
    digitalWriteFast(pinsDigitalOut[i], LOW);
  }

  for (int i=0; i<nPulsePins; i++) {
    pulsePins[i]->restart();
  }
  interrupts();
}

//Setting up parameters 
void setup() {
  
  pinMode(ledPin, OUTPUT);

  // analog input channels
  analogReadResolution(16); // change the resolution to 16 bits and read A0
  
  for (int i=0; i<nAnalogIn; i++) {
    pinMode(pinsAnalogIn[i], INPUT);
  }
  for (int i=8; i<nAnalogIn; i++) {
    pinMode(pinsAnalogIn[i], OUTPUT);
  }

  // digital input channel
  for (int i=0; i<nDigitalIn-2; i++) {
    pinMode(pinsDigitalIn[i], INPUT);
  }
  pinMode(CAMERA1,INPUT_PULLUP);
  pinMode(CAMERA2,INPUT_PULLUP);
  // digital output channels
  for (int i=0; i<nDigitalOut; i++) {
    pinMode(pinsDigitalOut[i], OUTPUT);
  }
  pinMode(VALVE,OUTPUT);
  // pinMode(LED1,OUTPUT);
  pinMode(GATHER_INDICATOR, OUTPUT);
  pinMode(LOOP_INDICATOR, OUTPUT);

  // Pulse
  for (int i=0; i<nPulsePins; i++) {
    pulsePins[i] = new PulsePin(i, pinsPulsePins[i], HIGH);
  }

  reset();

  //Start packet 
  packetSerialA.begin(57600);
  packetSerialA.setPacketHandler(&onPacketReceived);
  packetSerialA.setStream(&SerialUSB1);
  packetSerialB.begin(57600);
  packetSerialB.setPacketHandler(&onPacketReceived);
  packetSerialB.setStream(&SerialUSB2);

  // start data acquisition ticks, [us] interval
  // lowering priority is required to give the Encoder priority
  // and seems to massively reduce/prevent missed counts
  gatherTimer.priority(200);
  gatherTimer.begin(gather, 1000);

  // Synchronization pupil camera
  attachInterrupt(digitalPinToInterrupt(PIN_CAMERA_FSTROBE), syncBlink, CHANGE);
}


void loop() {

   digitalWriteFast(LOOP_INDICATOR, HIGH);

  // check serial status for data and buffer health
  packetSerialA.update();
  packetSerialB.update();

  if (updateSyncCounter) {
    updateSyncCounter = false;
    if (++syncCounterSubIdx > syncCounterFrameInterval) {
      syncCounterSubIdx = 0;
      if (++syncCounterIdx > 15) {
        syncCounterIdx = 0;
        if (++syncCounter > syncCounterMax) {
          syncCounter = syncCounterMin;
        }
      }
    }
  }

  
  if (packetReady) {
    State.crc16 = CRC16.kermit((uint8_t*) &State, sizeof(State));
    packetSerialA.send((byte*) &State, sizeof(State));
    packetSerialB.send((byte*) &State, sizeof(State));

    // apply current state vector
    applyState(&State);
    


    packetReady = false;
  }
  if (packetSerialA.overflow()) {
    EXTSERIAL.println("S_A overflow!");
  }

  if (packetSerialB.overflow()) {
    EXTSERIAL.println("S_B overflow!");
  }

  // check if timed pins need updates
  for (size_t i = 0; i < nPulsePins; ++i) {
    pulsePins[i]->updateMicro();
  }


  digitalWriteFast(LOOP_INDICATOR, LOW);

}

void gather() {
  digitalWriteFast(GATHER_INDICATOR, HIGH); // toggle pin to indicate gather start
  dataPacket packet;

   if(ephys==1){
      ephysrand();
   }
   else{
    digitalWriteFast(ephys_sync,LOW);
   }

   if(exper==1){
    runExperiment();
   }

   if(preshock==1){
    runPreShock();
   }


  packet.us_start = current_micros;
  
  for (int i=0; i<nAnalogIn; i++) {
    packet.analog[i] = analogRead(pinsAnalogIn[i]);
  }
  
  for(int i=0; i<nDigitalOut; i++){
      packet.digitalOut |= digitalReadFast(pinsDigitalOut[i]) << i;
  }
  
  for (int i=0; i<nDigitalIn; i++) {
    packet.digitalIn |= digitalReadFast(pinsDigitalIn[i]) << i;
  }
  
  camera_trigger= digitalReadFast(trigger_c);
  reward = digitalReadFast(REWARD);
  ephys =digitalReadFast(ephys_trigger);
  exper =digitalReadFast(EXPER);
  preshock=digitalReadFast(PRESHOCK);
  digitalWriteFast(CAMERA1,HIGH);
  digitalWriteFast(CAMERA2,HIGH);

  
  

 // With the IR lick detector we dont need a moving average anymore
  lick = analogRead(LICK);

  if (lick >= lickThresh)
  {  
    //digitalWriteFast(BLICK,HIGH); // redundant?
    digitalWriteFast(LICKDETECT,HIGH); // redundant?
    binaryLick = true;
  }
  else{
    //digitalWriteFast(BLICK,LOW);
    digitalWriteFast(LICKDETECT,LOW);
    binaryLick = false;
  }

  digitalWriteFast(VALVE,reward);

  noInterrupts();
  long new_pos = wheelEncoder.read(); // negative or positive?
  long position_feel = new_pos * 0.061; // 0.0159
  interrupts();

  for (int p=0; p<8; p++) {
    packet.variables[p] = 0L;
  }

  packet.variables[0] = new_pos;
  packet.variables[1] = position_feel;
  packet.variables[2] = binaryLick;
  packet.variables[3] = lick;
  packet.variables[4] = 0;
  packet.variables[5] = 0;
  packet.variables[6] = 0;
  packet.variables[7] = last_packet_took;
  packet.us_end = current_micros;

  last_packet_took = current_micros - packet.us_start;
  State = packet;
  packetReady = true; 
  digitalWriteFast(GATHER_INDICATOR, LOW); // toggle pin to indicate gather end
}  

void dumpBuffer(const uint8_t* buffer, size_t size) {
    EXTSERIAL.println(size, DEC);
    for (size_t i=0; i<size; i++) {
      EXTSERIAL.print(buffer[i], HEX);
      EXTSERIAL.print(' ');
    }
    EXTSERIAL.println(' ');
}

// If info received
void onPacketReceived(const uint8_t* buffer, size_t size) {
  // if we receive a command, do what it tells us to do...
  if (buffer[0] == ptINSTR) {
    processInstruction(buffer, size);
  } else {
    dumpBuffer(buffer, size);
  }
}

//State machine
void applyState(dataPacket* packet) {
  // apply finite state machine updates here
  counter++;
}

//PulsePin
PulsePin* getPulsePinById(byte id){
  for (size_t i = 0; i < nPulsePins; ++i) {
      if (pulsePins[i]->getId() == id){
        return pulsePins[i];
      }
  }
  return 0;
}

// synchronization pattern linking the camera to the teensy timing by
// sending a pulsed pattern. The pattern is a counter clocked by FSTROBE
// signal from the camera.
void syncBlink() {
  if (!digitalReadFast(PIN_CAMERA_FSTROBE)) {
    digitalWriteFast(PIN_SYNC_LED, (syncCounter >> syncCounterIdx) & 0x1);
    updateSyncCounter = true;
  } else {
  }
}


void processInstruction (const uint8_t* buf, size_t buf_sz) {
//  struct instructionPacket* ip = (struct instructionPacket*)buf;
//  char* data = (char*)ip->data;
  uint8_t instruction = buf[4];

  uint8_t stride;
  uint8_t target;
  uint8_t pin;
  bytesToLong bul;

  switch(instruction){
      case instPIN_LOW:
        stride = strideInstLOW;
        for (uint8_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
           target = buf[pIdx];
           pin = pinsDigitalOut[target];
           digitalWriteFast(pin, LOW);
        }
        break;

      case instPIN_HIGH:
        stride = strideInstHIGH;
        for (uint8_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
          target = buf[pIdx];
          pin = pinsDigitalOut[target];
          digitalWriteFast(pin, HIGH);
        }
        break;

      case instPIN_TOGGLE:
        stride = strideInstTOGGLE;
        for (uint8_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
          target = buf[pIdx];
          pin = pinsDigitalOut[target];
          digitalWriteFast(pin, !digitalReadFast(pin));
        }
        break;

       case instUNITY:
        stride = strideInstUNITY;
        for (uint8_t pIdx = 5; pIdx +   c       ride <= buf_sz; pIdx += stride) {
          target = buf[pIdx];
          pin = pinsDigital[target];
          digitalWrite(pin, !digitalRe  c       (pin));
        }
        break;


      case instPIN_PULSE:
        stride = strideInstPULSE;
        for (uint8_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
          target = buf[pIdx];
          for (size_t b=0; b<sizeof(bul); b++) {
            bul.bytes[b] = buf[pIdx+1+b];
          }
          pulsePins[target]->pulseMicro(bul.ulong*1000);
        }
        break;

      case instSET_STATE:
        stride = strideInstSTATE;
        EXTSERIAL.println("instSet_State");
        for (uint8_t pIdx = 5; pIdx + stride <= buf_sz; pIdx += stride) {
          target = buf[pIdx];
          for (size_t b=0; b<sizeof(bul); b++) {
            bul.bytes[b] = buf[pIdx+1+b];
          }
          EXTSERIAL.print(target, DEC);
          EXTSERIAL.print(':');
          EXTSERIAL.println(bul.slong);
          bufferedStates[target] = bul.slong;
        }
        break;

      case instRESET:
        EXTSERIAL.println("Resetting everything!");
        reset();
        break;

      default:
        EXTSERIAL.println("Unknown command");
        break;
  }
}


void ephysrand(){
  if (ephys==1) {
    digitalWriteFast(ephys_sync, (syncCounter >> syncCounterIdx) & 0x1);
    updateSyncCounter = true;
  } else {
  }

  
}
  
void runExperiment() {
  iPacket++;  // Increment iPacket
  static int offInterval = random(12000, 40001);  // Generate initial random off interval in milliseconds (12 to 40 seconds)
  static int trialCount = 0;  // To track the number of trials

  // Run 10 repetitions
  if (trialCount < 10) {
    // Off period: Keep pin LOW for a random time between 12 and 40 seconds
    if (iPacket == offInterval) { 
      digitalWriteFast(SHOCK, HIGH);
      digitalWriteFast(TESTSHOCK, HIGH);           
      digitalWriteFast(LED_BUILTIN, HIGH);
    }

    // On period: 2 seconds HIGH
    if (iPacket == offInterval + 2000) { 
      digitalWriteFast(SHOCK, LOW);          
      digitalWriteFast(TESTSHOCK, LOW);
      digitalWriteFast(LED_BUILTIN, LOW);
      iPacket = 0;  // Reset iPacket
      trialCount++;  // Increment the trial count
      offInterval = random(12000, 40001);  // Generate a new random off interval for the next trial
    }
  }
}




  void runPreShock() {
    iPacket++;
    // Run 3 repititions of 2 seconds HIGH, 30 seconds LOW
    for (int i = 0; i < 3; i++) {
      // Set pin 5 and the built-in LED to LOW for 30 seconds
      if(iPacket==10000){
        digitalWriteFast(SHOCK, HIGH);
        digitalWriteFast(TESTSHOCK, HIGH);           
        digitalWriteFast(LED_BUILTIN, HIGH);
      }

      if(iPacket==12000){
        digitalWriteFast(SHOCK, LOW);          
        digitalWriteFast(LED_BUILTIN, LOW);
        digitalWriteFast(TESTSHOCK, LOW);
        iPacket=0;
      }
    }
  }
