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
  

    ft.init(2, &Wire1, 0x60, &Wire, 0x62);  //  DACs for heading (on Wire1 bus- 0x60) + index (on Wire bus - 0x62)

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
