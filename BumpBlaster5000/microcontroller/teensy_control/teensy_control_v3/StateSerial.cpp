// File purpose:
// Implements the StateSerial class, which reads serial messages from Python,
// parses them into commands plus parameters, and marks when a full command is ready.
//
// Main responsibilities:
// - read serial input one character at a time from the Python interface
// - split incoming messages into comma-delimited fields
// - interpret the first field as cmd_len and the second as cmd
// - store all remaining fields into val_arr[]
// - raise new_cmd when a complete command has been received
// - reset parsing state if a message times out before completion
//
// Key additions / special logic:
// - recv_data() accumulates characters until a delimiter or newline is reached
// - read_state() interprets fields according to the message format:
//     <cmd_len>,<cmd>,<param0>,<param1>,...,<paramN>\n
// - cmd_len specifies how many parameters should follow the command ID
// - cmd specifies which execute_state() case will run in teensy_control_v3.ino
// - parameters are stored in val_arr[] as doubles for downstream use
// - new_cmd is set only after the full message has been received
// - timeout logic prevents partial/incomplete commands from hanging indefinitely;
//   on timeout, parsing resets and the system returns to closed loop (cmd = 5)
//
// IMPORTANT: This file enables runtime control from Python
// - this parser is how Python experimental protocols communicate with the Teensy
// - auto-blanking configuration, including threshold, is sent as a command message
// - for example, teensy_control_v3.ino case 16 reads values parsed here and passes
//   them into FTHandler::configure_auto_blank(...)
// - this is why threshold changes can be made in Python without changing FTHandler.cpp
//
// Relationships to other files:
// - StateSerial.h declares the class and shared parsing state used here
// - teensy_control_v3.ino calls read_state() every loop and checks new_cmd
// - teensy_control_v3.ino interprets cmd/val_arr[] in execute_state()
// - FTHandler is indirectly controlled through commands parsed here
//
// In short:
// This file is the serial command parser that converts Python messages into
// structured Teensy commands, making runtime experiment control possible.

#include "StateSerial.h"


StateSerial::StateSerial(Stream& srl_ref) : srl(srl_ref) {}

void StateSerial::recv_data() {
    static byte ndx = 0; // buffer index
    static char delimiter = ','; // column delimiter
    static char endline = '\n'; // endline character
    char curr_byte; // current byte;
    

    if (srl.available() > 0){
        data_rcvd_timestamp = millis();
        curr_byte = srl.read();
        if ((curr_byte == endline) | (curr_byte==delimiter)) { // end of frame or new column
            chars[ndx] = '\0'; // terminate string
            ndx = 0;
            new_data = true;
        } 
        else {
            chars[ndx] = curr_byte;
            ndx++;
            if (ndx >= num_chars) {
                ndx = num_chars - 1;
            } 
        }
    }
}

/*
State machine message format (from Python → Teensy):

<cmd_len>,<cmd>,<param0>,<param1>,...,<paramN>\n

Where:

cmd_len  = number of parameters that follow
cmd      = command ID (see execute_state() in teensy_control_v3.ino switch statement)
paramX   = parameters specific to that command

Example:
"1,7,0\n"

cmd_len = 1          → 1 parameter follows
cmd     = 7          → execute_state() case 7 (set index DAC)
param0  = 0          → set index to 0
*/

void StateSerial::read_state() {
    static int msg_timeout = 1000;
    static int begin_msg_timestamp = -1; 
 
    StateSerial::recv_data();
    if (new_data) {
//        SerialUSB2.println(cmd_index);
        if (cmd_index==0) { // First value = number of parameters in this message
            cmd_len = atoi(chars);
            SerialUSB2.print("DEBUG cmd_len parsed = "); // debug
            SerialUSB2.println(cmd_len); // debug
            begin_msg_timestamp = millis();
        } 
        else if (cmd_index == 1) { // Second value = command ID (determines which case runs in execute_state())
            cmd = atoi(chars);
            SerialUSB2.print("DEBUG cmd parsed = "); // debug
            SerialUSB2.println(cmd); // debug
        }
        else { // Remaining values = command parameters (stored in val_arr)
            val_arr[cmd_index-2] = atof(chars);
            SerialUSB2.print("DEBUG param["); // debug
            SerialUSB2.print(cmd_index-2); // debug
            SerialUSB2.print("] parsed = "); // debug
            SerialUSB2.println(val_arr[cmd_index-2], 6); // debug
        }
        
        cmd_index +=1; // update index
        if ((cmd_index-2) == cmd_len) { // if reached end of state machine message
            begin_msg_timestamp=-1;
//                execute_state(cmd, cmd_len);
            SerialUSB2.print("DEBUG full command ready | cmd_len = "); // debug
            SerialUSB2.print(cmd_len); // debug
            SerialUSB2.print(" | cmd = "); // debug
            SerialUSB2.print(cmd); // debug
            SerialUSB2.print(" | first param = "); // debug
            SerialUSB2.println(val_arr[0], 6); // debug
            
            cmd_index = 0;
            new_cmd=true;
        }
        new_data = false;
    }

    if (((millis()-begin_msg_timestamp)>msg_timeout)
        & (begin_msg_timestamp>0)) { // if command isn't read before timeout
        //abort 
        cmd = 5; // return to closed loop
        cmd_len = 0;
        cmd_index = 0;
        begin_msg_timestamp = -1;
        SerialUSB2.println("Timeout");
        new_cmd=true;
    }        
}
