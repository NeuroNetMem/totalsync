//
// Created by Francesco Battaglia on 07/09/2026.
//

// This is a template experiment definition. To use in totalsync, you can make a copy of the template experiment file,
// change the name of the Experiemnt subclass and the name of the .cpp file, and specify the pinout in the experiment_config.h file
// whose name needs to stay unchanged.


#include "experiment_config.h"

namespace {
    // rename to something you like, consistently. The definition is private to this file, so all edits should be
    // in here.
    class TemplateExperiment : public Experiment {

    private:
        // here you can define all the variables that you need to maintain the state of your experiment, for example
        // int state_variable_1 = 0;
        // int state_variable_2 = 0;
        // float state_variable_3 = 0.0;

        // as well as helper functions that you may need, for example
        // void runProtocol();

    public:
        // this can stay as is (with the class name changed)
        ~TemplateExperiment() override = default;

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
void TemplateExperiment::setup() {
    ;
}

void TemplateExperiment::loopMicro() {
    ;
}

void TemplateExperiment::loopMilliPre() {
   ;
}

void TemplateExperiment::loopMilliPost() {
    // default e.g. lick detection logic is defined in the superclass, it can be changed if needed
  Experiment::loopMilliPost();
}

void TemplateExperiment::reset() {
    ;
}




// generate the experiment object of the proper class, defined here
Experiment *makeExperiment() { return new TemplateExperiment(); }