// * FTHandler.cpp - handles receiving and parsing FicTrac data, updating DAC outputs for closed-loop control, 
// * and implementing auto-blanking based on motion thresholds 

// File purpose:
// Implements the FTHandler class, which receives and parses FicTrac serial data,
// updates heading and index DAC outputs, and applies optional motion-threshold-based
// auto-blanking of the visual bar.
//
// Main responsibilities:
// - read incoming FicTrac data one column at a time from the serial stream
// - parse key FicTrac columns such as frame number, heading, delta rotations, and delta timestamp
// - compute heading values used for closed-loop scene control
// - manage direct and delayed updates to heading and index DAC outputs
// - compute a rolling motion metric (mm/s) from FicTrac rotational velocities
// - automatically set the index DAC to visible or blank values *when auto-blanking is enabled*
//
// Key additions / special logic:
// - recv_data() reads FicTrac serial input incrementally and marks when a full column has been received
// - update_col() interprets the current FicTrac column and stores relevant values in FTHandler state
// - heading is parsed from FicTrac column 17 and used for closed-loop heading control
// *- delta rotations from FicTrac columns 2, 3, and 4 plus delta timestamp from column 24
// *  are used to compute a motion metric in mm/s
// - a single scalar "motion metric" is defined as the max velocity across axes
// -  a rolling window is used to compute mean motion to reduce noise
// - update_index_from_motion_threshold() applies the user-selected single-threshold rule:
//     - mean motion above threshold -> set index to visible value
//     - mean motion at or below threshold -> set index to blank value
//
// IMPORTANT: Threshold is NOT hardcoded here
// - although a default threshold exists, this file does NOT assume it is fixed
// - the value of motion_threshold_mm_per_s is updated at runtime via:
//     FTHandler::configure_auto_blank(...)
// - this function is called from teensy_control_v3.ino in response to Python commands
// - therefore, changing the threshold in Python is sufficient to change behavior
//   without modifying or recompiling this file
// 
// Practical implication:
// - create multiple experimental protocols in Python with different thresholds
// - each protocol sends its own threshold to the Teensy before/during the experiment
// - FTHandler will use the most recently received threshold value
// 
// Relationships to other files:
// - FTHandler.h declares the class and parameters used here
// - teensy_control_v3.ino:
//     - parses incoming commands from Python
//     - calls configure_auto_blank() to update threshold and related parameters
// - Python/BumpBlaster:
//     - defines experimental protocols
//     - sends threshold + enable/disable commands to Teensy
//
// In short:
// This file performs the actual computation: it converts FicTrac motion into a
// smoothed motion metric and uses a runtime-configurable threshold to decide
// whether the visual bar should be visible or blank.

#include "Arduino.h"
#include "FTHandler.h"

FTHandler::FTHandler(Stream& srl_ref): srl(srl_ref) {    
    }

void FTHandler::init(int f_pin, TwoWire* w1, uint8_t addr1, TwoWire* w,
                    uint8_t addr2, uint8_t intx_addr, uint8_t inty_addr) {
    // initialize frame pin
    // * pinMode and digitalWrite moved to init to ensure pin is set up before processing any FicTrac data which cues on frame pin toggling

    frame_pin = f_pin;
    pinMode(frame_pin,OUTPUT);
    digitalWriteFast(frame_pin,LOW);

    h_addr = addr1;
    i_addr = addr2;
    // initialize dacs — setClock must be called AFTER begin() on each bus,
    // because begin() resets the clock to the 100 kHz default
    // Wire1: heading (0x62) + intx (intx_addr)
    heading_dac.begin(h_addr, w1);
    intx_dac.begin(intx_addr, w1);
    w1->setClock(400000);
    // Wire: index (0x62) + inty (inty_addr)
    index_dac.begin(i_addr, w);
    inty_dac.begin(inty_addr, w);
    w->setClock(400000);

}

void FTHandler::recv_data() { // receive Fictrac data
    
    static uint8_t ndx = 0; // buffer index AL changed because byte was ambiguous in arduino
    static char delimiter = ','; // column delimiter
    static char endline = '\n'; // endline character
    static char curr_byte; // current byte

    static int _col_tmp;

//  * reading from serial until no more data, parsing into columns based on delimiter and endline characters
//  * and storing in chars buffer, cueing new_data when a full column is received

    if (srl.available() > 0) { // cannot use while(Serial.available()) because Teensy will read all 
        curr_byte = srl.read(); 
        if ((curr_byte == endline)|(curr_byte==delimiter)) { // end of frame or new column      
            chars[ndx] = '\0'; // terminate the string
            ndx = 0; // restart buffer index
            new_data = true;   // cue new data

            if (curr_byte == endline) { // checks that columns are being counted correctly
                _col_tmp = col + 1;
                if (_col_tmp != (num_cols)) {
                    col = num_cols-1;
                }
            }
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
//  * what update col does:
//  * checks which column of FicTrac data is being received (tracked by col variable that increments with each new column
//  * and resets after reaching num_cols)
//  * parses relevant columns (e.g., heading, delta rotations, delta timestamp) and stores in FTHandler variables
//  * for use in closed-loop control and auto-blanking logic
//  * changes made: added parsing of delta rotation and delta timestamp columns
//  * and added logic to compute motion metric and update index based on motion threshold

    void FTHandler::update_col() {

        // switch case statement for variables of interest
        switch (col) {
            
            case 0: // new FicTrac frame
                // flip ft pin high
                digitalWriteFast(frame_pin,HIGH);
                break;

            case 1: // frame counter
                current_frame = atoi(chars);
                break;

// * parse heading value from FicTrac and store in ft_heading variable, applying heading offset and wrapping to [0, 2pi) if in closed-loop mode.
// * new_heading flag is set to cue update_dacs() to update the DAC output for heading.
// * frame pin is flipped low at the start of update_col() and will be flipped high at the start of the next frame (case 0)
// * to provide a timing signal for when new FicTrac data is being processed.

            case 17: // heading 
                // flip ft pin low
                digitalWriteFast(frame_pin,LOW);

                // update heading pin
                ft_heading = atof(chars); // + PI;
                if (closed_loop) {
                    new_heading = true;
                }
                break;

// AL added case for XYZ rotation and delta timestamp for auto-blanking

            case 2: // FicTrac col 2: delta rotation x (camera coordinates)
                delta_rotation_x_cam = atof(chars);
                break;

            case 3: // FicTrac col 3: delta rotation y (camera coordinates)
                delta_rotation_y_cam = atof(chars);
                break;

            case 4: // FicTrac col 4: delta rotation z (camera coordinates)
                delta_rotation_z_cam = atof(chars);
                break;

            case 15: // FicTrac col 15: integrated x (lab coords, radians)
                integrated_x = atof(chars);
                new_intx = true;
                break;

            case 16: // FicTrac col 16: integrated y (lab coords, radians)
                integrated_y = atof(chars);
                new_inty = true;
                break;

            case 24: // FicTrac col 24: delta timestep since last frame (nanoseconds -> convert to seconds)
                delta_timestamp_sec = atof(chars) / 1e9;

        // *compute motion metric and update index if auto-blanking enabled
        // *1e-6 added to avoid divide-by-zero in case of very small delta timestamps which can occur when FicTrac is running at high frame rates
        
                if (delta_timestamp_sec > 1e-6) {
                    // Convert delta rotations to angular velocities (rad/s-ish) then to mm/s
                    double velocity_x_mm_per_s =
                        fabs(delta_rotation_x_cam / delta_timestamp_sec) * ball_radius_mm;

                    double velocity_y_mm_per_s =
                        fabs(delta_rotation_y_cam / delta_timestamp_sec) * ball_radius_mm;

                    double velocity_z_mm_per_s =
                        fabs(delta_rotation_z_cam / delta_timestamp_sec) * ball_radius_mm;

                    // reduce to one scalar: "Any motion" metric = maximum component velocity
                    float motion_metric_mm_per_s =
                        (float)std::max(velocity_x_mm_per_s, // max(...) : if any axis shows enough movement, count the fly as moving
                            std::max(velocity_y_mm_per_s, velocity_z_mm_per_s));
                            
            // *helpers are used to add motion metric samples to a rolling window and compute the mean motion metric over recent frames
            // *and if auto-blanking is enabled, the index DAC is set to blank or visible values based on whether mean motion metric exceeds the threshold

                    // Add to rolling mean window and update index if enabled
                    push_motion_metric_sample(motion_metric_mm_per_s);
                    update_index_from_motion_threshold();
                }
                break;
        }
    }

// what execute_state does:
//  * executes commands received from Python interface (cued by new_cmd flag in StateSerial)
//  * commands are parsed in StateSerial::read_state() and stored in cmd (command ID) and val_arr (command parameters)
//  * execute_state uses a switch-case statement to determine which command to execute based on cmd
//  * cmd under execute_state() in teensy_control_v3.ino
//  * for example, case 6 sets the heading DAC value based on val_arr[0] which is parsed from the Python command
//  * and case 7 sets the index DAC value based on val_arr[0]
    void FTHandler::execute_col() {
        FTHandler::recv_data(); 
        if (new_data == true) {
            FTHandler::update_col();
            col = (col+1) % num_cols; // keep track of columns in FicTrac data  
            new_data = false;
        }
    }
    
    void FTHandler::process_srl_data(){
        FTHandler::execute_col();
        if (closed_loop) {
            heading = fmod(ft_heading + heading_offset, 2*PI);
        } 
    }

    void FTHandler::update_dacs() {
        // check on delay timers
        curr_time = millis();
        if (heading_countdown.on_delay ) {
            if (curr_time > (heading_countdown.timestamp + heading_countdown.delay)) {
                heading = heading_countdown.val;
                heading_countdown.on_delay = false;
                new_heading = true;
            }
        }
//
        if (index_countdown.on_delay) {
            if (curr_time > (index_countdown.timestamp + index_countdown.delay)) {
                index = index_countdown.val;
                index_countdown.on_delay = false;
                new_index = true;
            }
        }

//          set dac vals
        if (new_heading){
          heading_dac.setVoltage(int(double(max_dac_val) * heading/2.0/PI), false);
          new_heading = false;
        }
        if (new_index) {
          index_dac.setVoltage(int(index), false);
          new_index = false;
        }
        if (new_intx) {
            double wrapped_x = fmod(integrated_x, 2*PI);
            if (wrapped_x < 0) wrapped_x += 2*PI;
            intx_dac.setVoltage(int(double(max_dac_val) * wrapped_x / (2*PI)), false);
            new_intx = false;
        }
        if (new_inty) {
            double wrapped_y = fmod(integrated_y, 2*PI);
            if (wrapped_y < 0) wrapped_y += 2*PI;
            inty_dac.setVoltage(int(double(max_dac_val) * wrapped_y / (2*PI)), false);
            new_inty = false;
        }

    }
// * set_heading and set_index are called from execute_state() in teensy_control_v3.ino when commands are received from Python interface (GUI)
// * to set heading and index DAC values directly (e.g., for open-loop control or testing)

    void FTHandler::set_heading(double h) {
        heading = fmod(h, 2.0*PI);
        new_heading = true;
    }

    //void FTHandler::set_heading(double h) {
        //SerialUSB2.print("DEBUG FTHandler::set_heading received h = ");
        //SerialUSB2.println(h, 6);

        // Clamp to valid 12-bit range
        //if (h < 0.0) h = 0.0;
        //if (h > 4095.0) h = 4095.0;

        // Map 0..4095 -> 0..2pi
        //heading = (h / 4096.0) * 2.0 * PI;

        //SerialUSB2.print("DEBUG FTHandler::set_heading stored heading = ");
        //SerialUSB2.println(heading, 6);

        //new_heading = true;
    //}


    void FTHandler::set_index(int i) {
        index = std::max(std::min(i, max_dac_val),0);
        new_index = true;
    }

    void FTHandler::set_heading_offset(double o) {
        heading_offset = o;
    }

    void FTHandler::rotate_scene(double r) {
        heading_offset = fmod(heading_offset + r, 2.0*PI);
    }

    int FTHandler::get_index() {
        return index;
    }

    void FTHandler::set_heading_on_delay(int t, double h){
        heading_countdown.on_delay = true;
        heading_countdown.delay = t;
        heading_countdown.timestamp = millis();
        heading_countdown.val = fmod(h, 2.0*PI);
    }

    void FTHandler::set_index_on_delay(int t, int i ) {
        index_countdown.on_delay = true;
        index_countdown.delay = t;
        index_countdown.timestamp = millis();
        index_countdown.val = std::max(std::min(i, max_dac_val),0);
    }

    // AL blanking added
    void FTHandler::set_auto_blank_enabled(bool enabled) {
        auto_blank_enabled = enabled;
    }

    // old single-threshold version (kept for reference — restore to revert):
    // void FTHandler::configure_auto_blank(double radius_mm, double threshold_mm_per_s,
    //                                      int visible_index_dac_value, int blank_index_dac_value) {
    //     ball_radius_mm = radius_mm;
    //     motion_threshold_mm_per_s = threshold_mm_per_s;
    //     index_visible_dac_value = ...; index_blank_dac_value = ...;
    // }
    // hysteresis version: takes high (turn-on) and low (turn-off) thresholds separately
    void FTHandler::configure_auto_blank(double radius_mm,
                                        double threshold_high_mm_per_s,
                                        double threshold_low_mm_per_s,
                                        int visible_index_dac_value,
                                        int blank_index_dac_value) {
        ball_radius_mm = radius_mm;
        motion_threshold_high_mm_per_s = threshold_high_mm_per_s; // bar turns ON above this
        motion_threshold_low_mm_per_s  = threshold_low_mm_per_s;  // bar BLANKS below this
        index_visible_dac_value = std::max(std::min(visible_index_dac_value, max_dac_val), 0);
        index_blank_dac_value   = std::max(std::min(blank_index_dac_value,   max_dac_val), 0);

        // Reset rolling window so enabling starts cleanly
        motion_window_head = 0;
        motion_window_count = 0;
        motion_metric_sum = 0.0;
    }

    void FTHandler::push_motion_metric_sample(float motion_metric_mm_per_s) {
        if (motion_window_count < MOTION_WINDOW_SAMPLES) {
            motion_metric_window[motion_window_head] = motion_metric_mm_per_s;
            motion_metric_sum += motion_metric_mm_per_s;
            motion_window_count++;
        } else {
            // Overwrite oldest sample (ring buffer)
            motion_metric_sum -= motion_metric_window[motion_window_head];
            motion_metric_window[motion_window_head] = motion_metric_mm_per_s;
            motion_metric_sum += motion_metric_mm_per_s;
        }

        motion_window_head = (motion_window_head + 1) % MOTION_WINDOW_SAMPLES;
    }

    float FTHandler::mean_motion_metric_mm_per_s() const {
        if (motion_window_count == 0) return 0.0f;
        return (float)(motion_metric_sum / (double)motion_window_count);
    }

    void FTHandler::update_index_from_motion_threshold() {
        if (!auto_blank_enabled) return;

        float mean_motion = mean_motion_metric_mm_per_s();

        // old single-threshold logic (kept for reference — restore to revert):
        // float thresh = (float)motion_threshold_mm_per_s;
        // if (mean_motion > thresh) { set_index(index_visible_dac_value); }
        // else                      { set_index(index_blank_dac_value);   }

        // hysteresis logic: only change state when metric clearly crosses a threshold.
        // dead zone between threshold_low and threshold_high holds the current state,
        // preventing fly grooming / micromotion from flickering the bar.
        if (mean_motion > (float)motion_threshold_high_mm_per_s) {
            set_index(index_visible_dac_value); // clearly walking — show bar
        } else if (mean_motion < (float)motion_threshold_low_mm_per_s) {
            set_index(index_blank_dac_value);   // clearly stopped — blank bar
        }
        // else: in dead zone — do nothing, hold current state
    }