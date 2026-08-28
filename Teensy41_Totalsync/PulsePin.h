#ifndef PULSDPIN_H
#define PULSDPIN_H
#include <Arduino.h>

extern elapsedMillis current_millis;
extern elapsedMicros current_micros;

class PulsePin {
  
  private:
    int pinID;
  	int pinNr;
  	int polarity;
  	unsigned long nextChangeTime;
  	boolean run;
    unsigned long maxMicro = 4294967295;
    unsigned long maxPulseDur = 100000000;
    
  public:
    // Setup pin 
    PulsePin(int pinID, int pinNr, int polarity);
    
    // Check the states
    void update();

    void updateMicro();
    void pulseMicro(unsigned long duration);
	
  	// Stop updating etc.
  	void stop();
  	
  	void restart();
   
    void pulse(unsigned long duration);

    int getId();
};
#endif
