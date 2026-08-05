plot_ds, plot_buffer_time, fictrac_frame_rate = 10, 600, 450

FT_PC_PARAMS = {
    'teensy_input_com': 'COM8', # SerialUSB1 (StateSerial commands)
    'teensy_output_com': 'COM4', # SerialUSB2 (prints/events back)
    'pl_com': 'COM13', # not currently being used?
    'baudrate': 115200,
    'plot_buffer_length': int(fictrac_frame_rate*plot_buffer_time/plot_ds),
}
# pl_com and vr_com can stay as-is (won't be used if you're only running the VR interface on one computer)

PL_PC_PARAMS = {
    'wedge_resolution': 16,
    'teensy_com': 'COM13',
    'vr_com': 'COM12',
    'baudrate': 115200,
    'baseline_time': 60,  # buffer size for baseline in df/f in seconds
    'func_time': .1,  # buffer size for df/f numerator in seconds (some unit is off here I think)
    'bump_signal_time': 10  # bump plot buffer size in seconds
}

import socket
hostname = socket.gethostname()
