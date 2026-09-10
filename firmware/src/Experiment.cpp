//
// Created by Francesco Battaglia on 10/09/2026.
//
#include <Arduino.h>
#include "Experiment.h"

#include <experiment_config.h>

void Experiment::loopMilliPost() {
    // With the IR lick detector we don't need a moving average anymore
    lick = analogRead(LICK);

    if (lick >= lickThresh) {
        digitalWriteFast(LICKDETECT, HIGH); // redundant?
        binaryLick = true;
    } else {
        digitalWriteFast(LICKDETECT, LOW);
        binaryLick = false;
    }
}
