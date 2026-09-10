// This file represents a template configuration with basic functionalities, target at a rodent VR setup. This refers to
// the hardware in the setup in our lab, as explained in the documentation and the paper TODO
// No task logic is included, so for example no reward is ever delivered, etc. This has to be set in the experiment code
// (see TemplateExperiment.cpp in this directory)

#ifndef EXPERIMENT_CONFIG_H
#define EXPERIMENT_CONFIG_H
#include "Experiment.h"
// PINs in use on the Teensy
// 7 analog inputs, 16 digital inputs, 16 digital outputs pins are available, according to the following scheme
// const int pinsAnalogIn[] = {16, 17, 18, 19, 20, 21, 22};
//
// const int pinsDigitalIn[] = {0, 1, 2,  3,  4,  5,  6,  7,
//                              8, 9, 10, 11, 12, 13, 14, 15};
//
// const int pinsDigitalOut[] = {24, 25, 26, 27, 28, 29, 30, 31,
//                               32, 33, 34, 35, 36, 37, 38, 39};


// connections to a rotary encoded for a running wheel.
// TODO explain the electrical connection of the rotary encoder, which model of encoder is used etc.
#define WHEEL_ENC_PINA 2
#define WHEEL_ENC_PINB 3
#define WHEEL_ENC_SW 4
// TODO what's this?
#define BLICK 5

// input for the scanner frame clock of a 2p microscope, useful for timestamping and synchronization with e.g. stimulation
#define SCANNER_FRAME_CLOCK 6

// #define LED_1 8

// synchronization with a Pi Camera for e.g. pupil monitoring
// TODO explain connectivity with the Pi etc.
#define PIN_CAMERA_FSTROBE 12
// analog input for a infrared lick detector
// TODO explain model and electrical connection

// input pins for camera frames TODO check
#define CAMERA1 14
#define CAMERA2 15

#define LICK 17

#define BREATHING 22
// activates the electrovalve that delivers fluid reward TODO give model connectivity
#define VALVE 24

// logical outputs that enables manual reward delivery, for example from the totalsync webapp
#define REWARD 25

// control pins for the Basler cameras
#define BASLER1 30
#define BASLER2 31

// control pins for the imaging LED light sources
#define LED1 32
#define LED2 33
#define LED3 34
// digital version of the lick detector, thresholded
#define LICKDETECT 26

// activates a random bar code signal that can be used for e.g. synchronization with a microscope or with electrophysiology
#define EPHYS_SYNC 37

// used for synchronization with the camera (See PIN_CAMERA_FSTROBE)
#define PIN_SYNC_LED 38

// Pins used to scope the communication / acquisition timing (for interaction between the Teensy and the webapp,
// so cannot be used for anything else)
#define LOOP_INDICATOR 40
#define GATHER_INDICATOR 41

Experiment *makeExperiment();

#endif // EXPERIMENT_CONFIG_H
