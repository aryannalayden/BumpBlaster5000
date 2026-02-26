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

    double heading_offset=0;

    
    int frame_pin;

    int max_dac_val = 4095;
    int num_cols = 26; 

    int col=0;
    int curr_time;

    Stream& srl;  

    Adafruit_MCP4725 heading_dac;
    Adafruit_MCP4725 index_dac;

    
    void recv_data();
    void update_col();
    void execute_col();

    public: 

        int current_frame = 0;
        bool closed_loop = true;
        
        FTHandler(Stream& srl_ref);
        void init(int f_pin, TwoWire* w1, uint8_t addr1, TwoWire* w, 
                    uint8_t addr2);
        
        void process_srl_data();
        void update_dacs();

        void set_heading(double h);

        void set_index(int i);

        void set_heading_offset(double o);

        void rotate_scene(double r);
        int get_index(); 

        void set_heading_on_delay(int t, double h);

        void set_index_on_delay(int t, int i );


};




#endif
