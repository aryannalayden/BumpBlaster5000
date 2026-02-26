#include "Arduino.h"
#include "FTHandler.h"

FTHandler::FTHandler(Stream& srl_ref): srl(srl_ref) {    
    }

void FTHandler::init(int f_pin, TwoWire* w1, uint8_t addr1, TwoWire* w, 
                    uint8_t addr2) {
    // initialize frame pin
    frame_pin = f_pin;

    pinMode(frame_pin,OUTPUT);
    digitalWriteFast(frame_pin,LOW);

    h_addr = addr1;
    i_addr = addr2;
    // initialize dacs
    heading_dac.begin(h_addr, w1);
    index_dac.begin(i_addr, w);    
    
}

void FTHandler::recv_data() { // receive Fictrac data
    
    static uint8_t ndx = 0; // buffer index AL changed because byte was ambiguous in arduino
    static char delimiter = ','; // column delimiter
    static char endline = '\n'; // endline character
    static char curr_byte; // current byte

    static int _col_tmp;

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

            case 17: // heading 
                // flip ft pin low 
                digitalWriteFast(frame_pin,LOW);

                // update heading pin
                ft_heading = atof(chars); // + PI;
                if (closed_loop) {
                    new_heading = true;
                }
                break;
        }
    }
    
    void FTHandler::execute_col() {
        FTHandler::recv_data(); 
        if (new_data == true) {
            FTHandler::update_col();
            col = (col+1) % num_cols; // keep track of columns in fictrack  
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

////          set dac vals
        if (new_heading){
          heading_dac.setVoltage(int(double(max_dac_val) * heading/2.0/PI), false);
          new_heading = false;
        }
        if (new_index) {
          index_dac.setVoltage(int(index), false);
          new_index = false;
        }

    }

    void FTHandler::set_heading(double h) {
        heading = fmod(h, 2.0*PI);
        new_heading = true;
    }

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
