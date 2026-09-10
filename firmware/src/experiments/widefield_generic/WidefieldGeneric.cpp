//
// Created by Francesco Battaglia on 07/09/2026.
//

// This is a template experiment definition. To use in totalsync, you can make a copy of the template experiment file,
// change the name of the Experiemnt subclass and the name of the .cpp file, and specify the pinout in the experiment_config.h file
// whose name needs to stay unchanged.


#include "experiment_config.h"
#include <Arduino.h>

namespace {
    // rename to something you like, consistently. The definition is private to this file, so all edits should be
    // in here.
    class WidefieldGeneric : public Experiment {

    private:
        // here you can define all the variables that you need to maintain the state of your experiment, for example
        // int state_variable_1 = 0;
        // int state_variable_2 = 0;
        // float state_variable_3 = 0.0;

        // as well as helper functions that you may need, for example
        // void runProtocol();
        //Moving average for lick detection
        static constexpr int windowSize = 10;    // size of moving average window
        int lickReadings[windowSize] = {}; // the readings from the lick input
        int windowIndex = 0;          // the index of the current reading
        int totalLickReadings = 0;    // the running total
        int averageLickReadings = 0;  // the average
        int lickThresh = 160;
        bool binaryLick = false;
        int lickBaselineCorr = 0;
        int lickBaseline = 0;
        int breathing = 0;

        int ipacket = 0;
        int frame = 0;

        void cameraprocess();

    public:
        [[maybe_unused]] static constexpr float EncoderConversion = 0.0159;


        // this can stay as is (with the class name changed)
        ~WidefieldGeneric() override = default;

        // these are the key entry points that are called in the Teensy loop. The signature needs to be `void method()` and all state info
        // passed via private class members.
        void setup() override; // gets called in setup(), when the Teensy is powered on. NOTE: if the code initializes hardware
                               // make sure that that hardware is on at that time
        void loopMicro() override; // gets called in the Teensy loop, even microsecond, so this is reserved for operations that require
                                   // fine granularity (e.g. handling time-sensitive hardware). It should be a lot weight function
        void loopMilliPre() override; // gets called in gather(), every millisecond before serial port communication and pin updates
        void loopMilliPost() override; // gets called in gather() every millisecong but after serial port and pin operations.
                                       // likely, all behavioral-task related operations will go in one of these two methods.
        void reset() override; // gets called in reset()

    };
}


// the implementation of the experiment class (here as a dummy, empty implementation)
void WidefieldGeneric::setup() {
    pinMode(CAMERA1, INPUT_PULLUP);
    pinMode(CAMERA2, INPUT_PULLUP);
}

void WidefieldGeneric::loopMicro() {
    ;
}

void WidefieldGeneric::loopMilliPre() {
   cameraprocess();
}

void WidefieldGeneric::loopMilliPost() {
    // lick detection logic with moving average
    totalLickReadings = totalLickReadings - lickReadings[windowIndex];
    lick = analogRead(LICK);
    lickReadings[windowIndex] = lick;
    totalLickReadings = totalLickReadings + lickReadings[windowIndex];
    averageLickReadings  = totalLickReadings / windowSize;
    windowIndex = windowIndex + 1;
    lickBaseline = averageLickReadings;
    lickBaselineCorr = lick - lickBaseline;

    // just to make sure it loops, we have to wrap it around
    if (windowIndex >= windowSize) {
        // ...wrap around to the beginning:
        windowIndex = 0;
    }

    if (lickBaselineCorr <= -lickThresh || lickBaselineCorr >= lickThresh) {
        digitalWriteFast(BLICK, HIGH); // redundant?
        binaryLick = true;
    }
    else {
        digitalWriteFast(BLICK, LOW);
        binaryLick = false;
    }
    breathing = analogRead(BREATHING);

}

void WidefieldGeneric::reset() {
    ;
}

void WidefieldGeneric::cameraprocess() {
    // apply finite state machine updates here
    ipacket++;
    if (ipacket == 3) {
        digitalWriteFast(BASLER1, HIGH);
        digitalWriteFast(BASLER2, HIGH);
        frame++;
    }
    else if (ipacket == 8) {
        digitalWriteFast(BASLER1, LOW);
        digitalWriteFast(BASLER2, LOW);
        ipacket = 0;
    }
    if ((frame % 2) == 0) {
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, HIGH);
        digitalWrite(LED3, HIGH);
    }
    else {
        digitalWrite(LED1, HIGH);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, LOW);
    }

}


// generate the experiment object of the proper class, defined here
Experiment *makeExperiment() { return new WidefieldGeneric(); }