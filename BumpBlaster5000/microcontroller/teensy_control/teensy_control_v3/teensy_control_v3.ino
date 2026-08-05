// File purpose:
// Top-level Teensy control file that coordinates communication between Python,
// FicTrac input, DAC output control, and hardware triggers for scanning/opto/pump.
//
// Main responsibilities:
// - initialize hardware triggers, DAC communication, and serial interfaces
// - continuously read FicTrac data through FTHandler
// - continuously read command packets from Python through StateSerial
// - route Python commands to the correct FTHandler / PointRunner actions
// - update DAC outputs and trigger states each loop
//
// Key additions / special logic:
// - FTHandler ft(Serial) handles FicTrac parsing and heading/index DAC control
// - StateSerial ss(SerialUSB1) receives commands from the Python/BumpBlaster interface
// - execute_state() is the main command dispatcher that maps ss.cmd values to actions
// - case 16 was added for runtime auto-blanking control:
//     - val_arr[0] = ball_radius_mm
//     - val_arr[1] = motion_threshold_mm_per_s
//     - val_arr[2] = visible_index_dac_value
//     - val_arr[3] = blank_index_dac_value
//     - val_arr[4] = enable flag (0 or 1)
// - case 16 calls:
//     - ft.configure_auto_blank(...)
//     - ft.set_auto_blank_enabled(...)
//   which means the Teensy updates the threshold and related parameters at runtime
//   based on values sent from Python
//
// IMPORTANT: Auto-blanking threshold is runtime-configurable
// - the threshold is NOT fixed in FTHandler.cpp once compiled
// - Python can send a new threshold at the start of an experiment using command 16
// - this file receives that command and passes the new threshold into FTHandler
// - therefore, changing the threshold in the Python experimental protocol is sufficient;
//   no C++ edits or reflashing are needed just to test a different threshold
// - however, each protocol should explicitly send its desired threshold/configuration,
//   otherwise FTHandler will continue using its current/default values
//
// Relationships to other files:
// - FTHandler.h declares the FTHandler interface and stored state
// - FTHandler.cpp implements FicTrac parsing, motion metric computation,
//   DAC updates, and threshold-based blanking logic
// - Python/BumpBlaster sends commands over serial, which are parsed by StateSerial
//   and executed here in execute_state()
//
// *In short:
// *This file is the central command-and-control layer on the Teensy: it connects
// *Python commands to FTHandler behavior, keeps FicTrac and DAC updates running,
// *and enables experiment-by-experiment control of auto-blanking parameters
// *(including motion threshold) without changing firmware logic.

// State reading variables
#include <cstring>
#include "Trigger.h"
#include "FTHandler.H"
#include "PointRunner.h"


#define BKSERIAL Serial6 // update to current pin settings


//Bruker Triggers
struct {
    Trigger trig;
    bool is_scanning;
} bk_scan;

Trigger opto_trig;
Trigger pump_trig;

FTHandler ft(Serial); // reads fictrac data and controls DACs

StateSerial ss(SerialUSB1); // reads commands from python interface

VisOptoPointRunner vis_opto_pr(opto_trig, ft, ss); // control visual stimulus and opto trigger
PumpOptoPointRunner pump_opto_pr(opto_trig, pump_trig, ss); // control microinjector pump trigger and opto trigger

void setup() {
  

    bk_scan.is_scanning = false;
    bk_scan.trig.init(3, 10, false); // initialize pin

    opto_trig.init(4, 10, false); // initialize pin
  
    // Pump trigger setup
    pump_trig.init(5, 500, true); // initialize pin, invert pin
  

    ft.init(2, &Wire1, 0x62, &Wire, 0x62, 0x63, 0x63);  // Wire1: heading(0x62)+intx(0x63)  Wire: index(0x62)+inty(0x63)

    BKSERIAL.begin(115200); // hardware serial
}

void yield() {} // get rid of hidden arduino yield function

FASTRUN void loop() {

    ft.process_srl_data(); // read fictrac data

    ss.read_state(); // read state machine serial port
    if (ss.new_cmd) { // if new state
        
        digitalWrite(13, !digitalRead(13));  // <-- ADD THIS LINE
        
        execute_state();
        ss.new_cmd = false;
    }

    ft.update_dacs(); // update DAC pins to control arena

    check_pins(); // flip triggers down, check stimulation timers
}




void execute_state() {

    switch(ss.cmd){
        case 0: // do nothing
            break;
        case 1: // flip start scan trigger high
            if (!bk_scan.is_scanning) {
                bk_scan.trig.trigger();
                bk_scan.is_scanning = true;

                SerialUSB2.print("start, "); // start trigger falling edge Fictrac frame
                SerialUSB2.print(ft.current_frame);
                SerialUSB2.print('\n');
            }
            break;
        case 2: // kill scan
            if (bk_scan.is_scanning) {
                bk_scan.trig.trigger();
                bk_scan.is_scanning = false; 
                
                SerialUSB2.print("abort, "); // abort trigger rising edge Fictrac frame
                SerialUSB2.print(ft.current_frame);
                SerialUSB2.print('\n');
                // SerialUSB2.println("END QUEUE");

                // send kill scan signal to PrarieView API
                BKSERIAL.println("-Abort");
            }
            break;

        case 3: // flip opto scan trigger high
            opto_trig.trigger();
            break;

        case 4: // set heading pin to manual control (i.e. open loop)
            ft.closed_loop=false;
            break;
        
        case 5: // go back to closed loop 
            ft.closed_loop = true;
            break;

        case 6: // set heading_dac value
            ft.set_heading(ss.val_arr[0]);
            break;

        case 7: // set index_dac value
            ft.set_index(ss.val_arr[0]);
            break;

        case 8: // set heading and index dac
            ft.set_heading(ss.val_arr[0]);
            ft.set_index(ss.val_arr[1]);
            break;

        case 9: // set heading and index dac, trigger opto with specified delay
            // heading, index, opto_bool, opto_delay, 0
            vis_opto_pr.start_points();
            break;

        case 10: // run list of points
            // each point: heading, index, opto_bool, opto_delay, combined_dur
            vis_opto_pr.start_points();
            break;

        case 11: // kill list of points
            vis_opto_pr.abort_points();
            break;

        case 12: // trig pump
            pump_trig.trigger();
            break;

        case 13: // trig pump and opto with specified delay
            // opto_bool, opto_delay
            pump_opto_pr.start_points();
            break;

        case 14: // set fictrac offset
            ft.set_heading_offset(ss.val_arr[0]);
            break;

        case 15: // rotate scene by set amount in radians
            ft.rotate_scene(ss.val_arr[0]);
            break;

        case 16: // AL configure and enable/disable auto-blanking
            // old single-threshold call (kept for reference):
            // ft.configure_auto_blank(ss.val_arr[0], ss.val_arr[1], (int)ss.val_arr[2], (int)ss.val_arr[3]);
            // hysteresis version: val_arr[1] = threshold_high, val_arr[2] = threshold_low
            ft.configure_auto_blank(ss.val_arr[0],   // radius_mm
                                    ss.val_arr[1],   // threshold_high_mm_per_s (turn-on)
                                    ss.val_arr[2],   // threshold_low_mm_per_s  (turn-off)
                                    (int)ss.val_arr[3], // visible DAC value
                                    (int)ss.val_arr[4]); // blank DAC value
            ft.set_auto_blank_enabled((bool)ss.val_arr[5]);
            break;
   }
}



void check_pins() {
    static int curr_time=0;
 
 
    curr_time = millis();

    // flip start down
    bk_scan.trig.check(curr_time);


    // flip opto trigger down
    opto_trig.check(curr_time);

    // flip opto trigger down
    pump_trig.check(curr_time);
    

    // run multipoint control
    vis_opto_pr.check_for_next_point(curr_time);
    pump_opto_pr.check_for_next_point(curr_time);

}










// col 1 frame counter
// col 2-4 delta rotation vector (x,y,z) cam coords
// col 5 delta rotation error
// col 6-8 delta rotation in lab coordinates
// col 9-11 abs. rot. vector cam coords
// col 12-14 abs. rot. vector lab coords
// col 15-16 integrated x/y lab coords
// col 17 integrated heading lab coords
// col 18 movement direction lab coords (add col 17 to get world centric direction)
// col 19 running speed. scale by sphere radius to get true speed
// col 20-21 integrated x/y neglecting heading
// col 22 timestamp either position in video file or frame capture time
// col 23 sequence counter - usually frame counter but can reset is tracking resets
// col 24 delta timestep since last frame
// col 25 alt timestamp - frame capture time (ms since midnight)
