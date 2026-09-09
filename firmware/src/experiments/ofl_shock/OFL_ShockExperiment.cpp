//
// Created by Francesco Battaglia on 07/09/2026.
//

#include <PacketSerial.h>

#include "experiment_config.h"

namespace {
    class OFL_ShockExperiment : public Experiment {

    private:
        int iPacket = 0;
        int exper = 0;
        int preshock = 0;
        void runExperiment();
        void runPreShock();

    public:
        ~OFL_ShockExperiment() override = default;
        void setup() override; // gets called in setup()
        void loopMicro() override; // gets called in loop()
        void loopMilliPre() override; // gets called in gather() before serial port and pin updates
        void loopMilliPost() override; // gets called in gather() after serial port and pin updates
        void reset() override; // gets called in reset()

    };
}

void OFL_ShockExperiment::setup() {
    ;
}

void OFL_ShockExperiment::loopMicro() {
    ;
}

void OFL_ShockExperiment::loopMilliPre() {

    if (exper == 1) {
      runExperiment();
    }

    if (preshock == 1) {
      runPreShock();
    }
}

void OFL_ShockExperiment::loopMilliPost() {
    exper = digitalReadFast(EXPER);
    preshock = digitalReadFast(PRESHOCK);

}

void OFL_ShockExperiment::reset() {
    ;
}

void OFL_ShockExperiment::runExperiment() {
    iPacket++; // Increment iPacket
    // Initial random off interval in milliseconds (12 to 40 seconds)
    static int offInterval = random(12000, 40001);
    static int trialCount = 0; // To track the number of trials

    // Run 10 repetitions
    if (trialCount < 10) {
        // Off period: keep pin LOW for a random time between 12 and 40 seconds
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
            iPacket = 0;      // Reset iPacket
            trialCount++;     // Increment the trial count
            // Generate a new random off interval for the next trial
            offInterval = random(12000, 40001);
        }
    }
}

void OFL_ShockExperiment::runPreShock() {
    iPacket++;
    // Run 3 repetitions of 2 seconds HIGH, 30 seconds LOW
    for (int i = 0; i < 3; i++) {
        // Set the shock pins and the built-in LED to LOW for 30 seconds
        if (iPacket == 10000) {
            digitalWriteFast(SHOCK, HIGH);
            digitalWriteFast(TESTSHOCK, HIGH);
            digitalWriteFast(LED_BUILTIN, HIGH);
        }

        if (iPacket == 12000) {
            digitalWriteFast(SHOCK, LOW);
            digitalWriteFast(LED_BUILTIN, LOW);
            digitalWriteFast(TESTSHOCK, LOW);
            iPacket = 0;
        }
    }
}


// generate the experiment object of the proper class, defined here
Experiment *makeExperiment() { return new OFL_ShockExperiment(); }