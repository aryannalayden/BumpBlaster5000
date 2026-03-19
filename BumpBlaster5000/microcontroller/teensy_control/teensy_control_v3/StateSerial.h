// File purpose:
// Declares the StateSerial class, which reads command messages sent from Python
// to the Teensy and stores the parsed command ID and parameters for later execution.
//
// Main responsibilities:
// - define the serial parsing state used for incoming Python command messages
// - store the current command ID, command length, and parsed parameter values
// - expose methods for receiving serial data and assembling complete commands
// - provide a simple interface that other files can query using:
//     - cmd_len
//     - cmd
//     - val_arr[]
//     - new_cmd
//
// Key additions / special logic:
// - incoming messages follow the format:
//     <cmd_len>,<cmd>,<param0>,<param1>,...,<paramN>\n
// - val_arr[] stores numeric command parameters after parsing
// - new_cmd is raised only when a full command has been received
// - data_rcvd_timestamp records when serial data was last received
// - this class only parses and stores commands; it does not decide what they mean
//
// IMPORTANT: This file is part of the runtime configuration pathway
// - Python experimental protocols send command messages through this interface
// - those messages can include auto-blanking configuration values such as threshold
// - StateSerial makes those parsed values available to teensy_control_v3.ino,
//   which then passes them into FTHandler
// - therefore, runtime parameters like the auto-blanking threshold can be changed
//   from Python without editing firmware logic
//
// Relationships to other files:
// - StateSerial.cpp implements the parsing logic declared here
// - teensy_control_v3.ino owns a StateSerial instance and checks new_cmd each loop
// - teensy_control_v3.ino reads cmd and val_arr[] and routes them through execute_state()
// - Python/BumpBlaster sends the command packets that this class parses
//
// In short:
// This file defines the command-parsing interface between Python and the Teensy,
// allowing experiment parameters and control commands to be updated at runtime.

#ifndef STATESERIAL_H
#define STATESERIAL_H


#include "Arduino.h"

class StateSerial {
    int num_chars = 256;
    char chars[256];
    bool new_data = false;
    int cmd_index = 0;
    

    Stream& srl;

    public:
        double val_arr[1024]; //204 points
        // ! does above line make sense? 1024 is arbitrary large number to allow for many parameters if needed; can be reduced if memory issues arise
        int cmd = 0;
        int cmd_len = 0;
        bool new_cmd = false;

        int data_rcvd_timestamp = -1;

        StateSerial(Stream& srl_ref);
        void recv_data();
        void read_state();
};


#endif
