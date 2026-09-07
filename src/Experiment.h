//
// Created by Francesco Battaglia on 07/09/2026.
//

#ifndef TOTALSYNCAATC_EXPERIMENT_H
#define TOTALSYNCAATC_EXPERIMENT_H

class Experiment {


public:
    Experiment() = default;
    virtual ~Experiment() = default;
    virtual void setup() = 0; // gets called in setup()
    virtual void loopMicro() = 0; // gets called in loop()
    virtual void loopMilliPre() = 0; // gets called in gather() before serial port and pin updates
    virtual void loopMilliPost() = 0; // gets called in gather() after serial port and pin updates
    virtual void reset() = 0; // gets called in reset()
};
#endif //TOTALSYNCAATC_EXPERIMENT_H
