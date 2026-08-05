// * FTHandler interface for parsing FicTrac serial data, tracking heading/index state,
// * and performing optional motion-threshold-based auto-blanking of the visual bar.
//
// Main responsibilities:
// - define all state variables for heading, index, and FicTrac parsing
// - store auto-blanking configuration parameters (radius, threshold, DAC values)
// - declare helper functions for motion metric calculation and threshold logic
// - expose public methods used by Teensy command interface (setters + configuration)
//
// Key additions / special logic:
// - includes runtime-configurable auto-blanking parameters:
//     - ball_radius_mm
//     - motion_threshold_mm_per_s
//     - index_visible_dac_value
//     - index_blank_dac_value
// - maintains a rolling window of motion samples for computing mean motion
// - exposes:
//     - set_auto_blank_enabled(bool)
//     - configure_auto_blank(...)
//   which allow parameters (including threshold) to be updated at runtime
//
// IMPORTANT: Threshold control is runtime-configurable
// - the motion threshold is NOT fixed at compile time
// - the value defined here is only a default
// - the actual threshold used during experiments is set by Python via
//   configure_auto_blank(), routed through teensy_control_v3.ino
// - this allows different experimental protocols to use different thresholds
//   without modifying or reflashing the Teensy firmware
//
// Relationships to other files:
// - FTHandler.cpp implements all methods declared here
// - teensy_control_v3.ino receives commands from Python and calls:
//     - set_auto_blank_enabled()
//     - configure_auto_blank()
// - Python/BumpBlaster experimental protocols send threshold + parameters
//   at the start of each experiment

// In short:
// This file defines the data structures and interface for FicTrac-driven control
// and motion-based auto-blanking, with all key parameters (including threshold)
// configurable at runtime from Python

#ifndef FTHANDLER_H
#define FTHANDLER_H

#include "Arduino.h"

#include <Adafruit_BusIO_Register.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_MCP4725.h>
#include <Wire.h>
#include <math.h>
#include <cstring>
#include <algorithm>
using namespace std;

const int num_chars = 256;

struct dac_countdown { // struct for storing info about when to set dac
    bool on_delay = false;
    int delay;
    int timestamp;
    double val;
};

class FTHandler { 
//    bool closed_loop = true; 
    char chars[num_chars]; 
    bool new_data = false;

    int h_addr;
    int i_addr;
    double heading;
    bool new_heading;
    dac_countdown heading_countdown;
    double ft_heading;
    int index;
    bool new_index;
    dac_countdown index_countdown;

    double integrated_x = 0.0;
    double integrated_y = 0.0;
    bool new_intx = false;
    bool new_inty = false;

    double heading_offset=0;
    // -------------------------
    // Auto-blanking based on FicTrac motion (optional / opt-in)
    // -------------------------
    bool auto_blank_enabled = false;

    // Configuration (units explicitly in mm and mm/s)
    double ball_radius_mm = 4.5;                 // ball radius
    // double motion_threshold_mm_per_s = 0.1;  // single-threshold (old). kept for reference; replaced by hysteresis below
    // Hysteresis thresholds: bar turns ON above threshold_high, blanks below threshold_low.
    // Dead zone between the two holds current state — prevents fly grooming/micromotion
    // from flickering the bar. To revert to single-threshold, re-enable the line above,
    // remove these two, and restore the single-threshold logic in update_index_from_motion_threshold().
    double motion_threshold_high_mm_per_s = 7.0; // metric must exceed this to show bar (above fly micromotion ~3-5 mm/s)
    double motion_threshold_low_mm_per_s  = 3.0; // metric must drop below this to blank bar (above FicTrac noise floor ~2 mm/s)

    // Index output values (DAC counts 0..4095)
    // Using 0 for "visible" and 2048 for "blank" ( > midpoint ~2048 )
    int index_visible_dac_value = 0;
    int index_blank_dac_value   = 4095; // change to 2048? anything above 5 volts will blank.

    // FicTrac parsed motion columns (camera coordinates)
    double delta_rotation_x_cam = 0.0;           // col 2
    double delta_rotation_y_cam = 0.0;           // col 3
    double delta_rotation_z_cam = 0.0;           // col 4
    double delta_timestamp_sec  = 0.0;           // col 24

    // Rolling window for motion metric (mean over last N frames)
    // Camera runs at ~108 fps. With hysteresis (threshold_high/low in configure_auto_blank)
    // handling grooming stability, a small window is sufficient for fast response.
    // Turn-off lag ≈ 0.9 × samples / fps.
    //
    // static const int MOTION_WINDOW_SAMPLES = 200; // ~1.7 s lag — stable but sluggish
    // static const int MOTION_WINDOW_SAMPLES = 108; // ~0.9 s lag — balanced
    static const int MOTION_WINDOW_SAMPLES = 65;  // ~0.5 s lag — fast; safe with good FicTrac tracking
    // Note: below ~40, burst tracking errors can spike mean above threshold_high
    float motion_metric_window[MOTION_WINDOW_SAMPLES];
    int motion_window_head = 0;
    int motion_window_count = 0;
    double motion_metric_sum = 0.0;

    // Helpers
    void push_motion_metric_sample(float motion_metric_mm_per_s);
    float mean_motion_metric_mm_per_s() const;
    void update_index_from_motion_threshold();
    
    int frame_pin;

    int max_dac_val = 4095;
    int num_cols = 26; 

    int col=0;
    int curr_time;

    Stream& srl;  

    Adafruit_MCP4725 heading_dac;
    Adafruit_MCP4725 index_dac;
    Adafruit_MCP4725 intx_dac;
    Adafruit_MCP4725 inty_dac;

    
    void recv_data();
    void update_col();
    void execute_col();

    public: 

        int current_frame = 0;
        bool closed_loop = true;
        
        FTHandler(Stream& srl_ref);
        void init(int f_pin, TwoWire* w1, uint8_t addr1, TwoWire* w,
                    uint8_t addr2, uint8_t intx_addr, uint8_t inty_addr);
        
        void process_srl_data();
        void update_dacs();

        void set_heading(double h);

        void set_index(int i);

        void set_heading_offset(double o);

        void rotate_scene(double r);
        int get_index(); 

        void set_heading_on_delay(int t, double h);

        void set_index_on_delay(int t, int i );
// AL blanking added
        void set_auto_blank_enabled(bool enabled);
        // old single-threshold signature (kept for reference):
        // void configure_auto_blank(double radius_mm, double threshold_mm_per_s,
        //                           int visible_index_dac_value, int blank_index_dac_value);
        // hysteresis version — takes both a high (turn-on) and low (turn-off) threshold:
        void configure_auto_blank(double radius_mm,
                                double threshold_high_mm_per_s,
                                double threshold_low_mm_per_s,
                                int visible_index_dac_value,
                                int blank_index_dac_value);


};




#endif
