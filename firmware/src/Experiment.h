//
// Created by Francesco Battaglia on 07/09/2026.
//

#ifndef TOTALSYNCAATC_EXPERIMENT_H
#define TOTALSYNCAATC_EXPERIMENT_H

class Experiment {


private:
    // default implementation of lick detection goes here. It may be overridden by the experiment, for example in the case of
    // different sensors
    static constexpr int lickThresh = 2600;



public:

    // default values for some constant that may be overridden by the experiment

    static constexpr float EncoderConversion = 0.077; // was 0.0159
    // data members for the lick detection logic
    bool binaryLick = false;
    int lick = 0;
    // the experiment API
    Experiment() = default;
    virtual ~Experiment() = default;
    virtual void setup() = 0; // gets called in setup()
    virtual void loopMicro() = 0; // gets called in loop()
    virtual void loopMilliPre() = 0; // gets called in gather() before serial port and pin updates
    virtual void loopMilliPost(); // gets called in gather() after serial port and pin updates
    virtual void reset() = 0; // gets called in reset()
};
#endif //TOTALSYNCAATC_EXPERIMENT_H
