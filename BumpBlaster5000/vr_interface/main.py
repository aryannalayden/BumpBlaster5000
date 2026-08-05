import os, threading, queue, time, pickle
import multiprocessing as mp

import numpy as np
import pyqtgraph as pg
from pyqtgraph import QtCore, QtGui, Qt
from pyqtgraph.Qt.QtWidgets import QFileDialog
import serial

from .. import params, shared_memory, experiment_protocols
from ..utils import threaded, numba_histogram, launch_multiprocess
from . import pg_gui

if params.hostname != 'bard-smaug-slayer':
    from . import fictrac_utils as ft_utils

class BumpBlaster(pg_gui.WidgetWindow):

    def __init__(self, plot_timeout = 30):
        super().__init__()

        ## com port params
        self._params = params.FT_PC_PARAMS

        ## Teensy push button connections
        self.start_scan_button.clicked.connect(self.start_scan)
        self.stop_scan_button.clicked.connect(self.stop_scan)
        self.trigger_opto_button.clicked.connect(self.trigger_opto)
        
        ## fictrac
        self.ft_frames = {'start': [], 'opto': [], 'abort': [], 'start_trig_falling_edge':[]}
        self.ft_output_path = None
        self.ft_manager = ft_utils.FicTracSocketManager()
        self.launch_fictrac_checkbox.stateChanged.connect(self.toggle_fictrac)
        self._ft_process = None
        self.send_orientation_checkbox.stateChanged.connect(self.toggle_send_orientation)
        self._send_orientation = threading.Event()

        self.pl_serial = None
        
        ## Load default opto experiment
        self.exp_combobox.currentTextChanged.connect(self.set_exp)
        exec(f"self.exp_func = experiment_protocols.{self.exp_combobox.currentText()}.run")
        self.exp_process = None
        self.run_exp_button.clicked.connect(self.run_exp)
        self.abort_exp_button.clicked.connect(self.abort_exp)


        ## start serial port to send commands to teensy
        try:
            self.teensy_input_serial = serial.Serial(self._params['teensy_input_com'], baudrate=self._params['baudrate'])
        except serial.SerialException:
            raise Exception("teensy input serial port %s couldn't be open" % self._params['teensy_input_com'])
        self.teensy_input_queue = mp.SimpleQueue()
        # reset Teensy to a known state on startup — the Teensy does not reset when
        # the GUI restarts, so any auto-blank state from the previous session persists.
        # send: disable auto-blank (cmd 16, enabled=0), set index visible (cmd 7), closed loop (cmd 5)
        self.teensy_input_queue.put(b'6,16,4.5,7.0,3.0,0,4095,0\n')  # disable auto-blank
        self.teensy_input_queue.put(b'1,7,0\n')                        # index visible
        self.teensy_input_queue.put(b'0,5\n')                          # closed loop
        

        ## start thread to read outputs from teensy
        self._isreading_teensy = threading.Event()
        self.teensy_read_queue = queue.SimpleQueue()
        self.teensy_read_handle = self.continuous_read_teensy_com()
        while not self._isreading_teensy.is_set():
            time.sleep(.01)

        self.teensy_write_handle = self.write_to_teensy_input_com()

        # plots
        self.reset_plots_button.clicked.connect(self.ft_manager.reset_plot_dequeus)
        self.plot_deques = {'integrated x': None,
                            'integrated y': None,
                            'heading': None}
        self.plot_update_timer = QtCore.QTimer()
        self.plot_update_timer.timeout.connect(self.update_plots)
        self.plot_update_timer.start(plot_timeout)
        
        # run once for compiling
        _ = numba_histogram(np.linspace(0,10), 5)
        
        
        # manually set heading or index pin
        self.heading_pin_send_button.clicked.connect(self.send_heading_val)
        self.index_pin_send_button.clicked.connect(self.send_index_val)

    def start_scan(self):
        '''

        :return:
        '''
        #ToDo: fix to write to input_queue
        self.teensy_input_queue.put(b'0,1\n')
        # self.teensy_input_serial.write(b'0,1\n')  # see teensy_control.ino
        self.start_scan_button.setEnabled(False)
        self.trigger_opto_button.setEnabled(True)
        self.stop_scan_button.setEnabled(True)

        

    def stop_scan(self):
        '''

        :return:
        '''
        self.teensy_input_queue.put(b'0,2\n')
        # self.teensy_input_serial.write(b'0,2\n')  # see teensy_control.ino
        while (len(self.ft_frames['abort']) == 0):
            time.sleep(.01)

        self.start_scan_button.setEnabled(True)
        self.trigger_opto_button.setEnabled(False)
        self.stop_scan_button.setEnabled(True)
        
        # save ft_frames
        if self.ft_output_path is not None:
            scan_number = 0
            basename, ext = os.path.splitext(self.ft_output_path)

            filename = os.path.join(f"{basename}_scan{scan_number}.pkl")
            while os.path.exists(filename):
                scan_number+=1
                filename = os.path.join(f"{basename}_scan{scan_number}.pkl")
            
            with open(filename,'wb') as file:
                pickle.dump(self.ft_frames,file)
                
            #ToDo: some issue going on here with values being overwritten
            self.ft_frames = {'start': [], 'opto': [], 'abort': [], 'start_trig_falling_edge':[]}
            # self.ft_frames = {'start': [], 'opto': [], 'abort': []}
            

        
    def trigger_opto(self):
        '''

        :return:
        '''
        self.teensy_input_queue.put(b'0,3\n')
        # self.teensy_input_serial.write(b'0,3\n')  # see teensy_control.ino


    def toggle_send_orientation(self):
        
        if self.send_orientation_checkbox.isChecked():
            try: 
                self.pl_serial = serial.Serial(self._params['pl_com'], baudrate = self._params['baudrate'])
            except serial.SerialException:
                raise Exception("prairie link serial port %s couldn't be opened" % self._params['prairie_link_com'])
            self._send_orientation.set()
        else:
            self._send_orientation.clear()
            self.pl_serial.close()
        
    def set_exp(self):

        exec(f"self.exp_func = experiment_protocols.{self.exp_combobox.currentText()}.run")
        

    def run_exp(self):
        if self.exp_process is not None:
            self.exp_process.join()
       
        self.exp_process = launch_multiprocess(self.exp_func, self.teensy_input_queue)

    def abort_exp(self):
        self.teensy_input_queue.put(b'0,11\n')         # abort PointRunner
        # clean up auto-blank state — kill() is immediate so the experiment's own
        # cleanup code never runs; send the reset here before killing the process
        self.teensy_input_queue.put(b'6,16,4.5,7.0,3.0,0,4095,0\n')  # disable auto-blank
        self.teensy_input_queue.put(b'1,7,0\n')                        # index visible
        self.teensy_input_queue.put(b'0,5\n')                          # closed loop
        self.exp_process.kill()
        
    def send_heading_val(self):
        new_heading = float(self.heading_pin_input.text())
        self.teensy_input_queue.put(f"1,6,{new_heading}\n".encode('UTF-8'))
        
    def send_index_val(self):
        new_index = int(self.index_pin_input.text())
        self.teensy_input_queue.put(f"1,7,{new_index}\n".encode('UTF-8'))
        
    def toggle_fictrac(self):
        '''

        :return:
        '''

        if self.launch_fictrac_checkbox.isChecked():
            
            # output path
            self.ft_output_path = QFileDialog.getSaveFileName(self.layout,
                                                               "FicTrac Output File")[0]
            
            
            self.ft_manager.open(output_path=self.ft_output_path)
            self.ft_manager.start_reading()
            self._pl_serial_thread = self.write_to_pl_com() # write to prairie link
            # initialize queues
            for k in self.plot_deques.keys():
                self.plot_deques[k] = shared_memory.CircularFlatBuffer(params.FT_PC_PARAMS['plot_buffer_length'], name = k).connect()
            
        else:
            self.ft_manager.stop_reading()
            self.ft_manager.close()
            self._pl_serial_thread.join()

    @threaded 
    def write_to_pl_com(self):
        while self.ft_manager.reading.is_set():
            if not self.ft_manager.ft_serial_queue.empty():
                heading = self.ft_manager.ft_serial_queue.get()
                if self._send_orientation.is_set():
                    self.pl_serial.write(f"{heading}\n".encode('UTF-8'))


    @threaded
    def write_to_teensy_input_com(self):
        while self._isreading_teensy.is_set():
            if not self.teensy_input_queue.empty():
                tmp = self.teensy_input_queue.get()
                self.teensy_input_serial.write(tmp) #self.teensy_input_queue.get())
    

        
    @threaded
    def continuous_read_teensy_com(self):
        '''

        :return:
        '''
        try:
            srl = serial.Serial(self._params['teensy_output_com'], baudrate=self._params['baudrate'])
        except serial.SerialException:
            raise Exception("teensy output serial port %s couldn't be opened" % self._params['teensy_output_com'])

        self._isreading_teensy.set()

        while self._isreading_teensy.is_set():
            while srl.inWaiting() > 0:
                msg = srl.readline().decode('UTF-8').rstrip().split(',')
                if msg[0] in set(('start', 'opto', 'abort', 'start_trig_falling_edge')):
                    self.ft_frames[msg[0]].append(int(msg[1]))
                else:
                    print(msg)
        srl.close()

            
    def update_plots(self):
        '''

        :return:
        '''

        if self.ft_manager.reading.is_set():

            self.plot_cumm_path()
            self.plot_heading_hist()
            
            
    def plot_cumm_path(self):
        with self.ft_manager._ft_buffer_lock:
            self.cumm_path_plotitem.plot(self.plot_deques['integrated x'].vals, self.plot_deques['integrated y'].vals,
                                         clear=True, _callSync='off')

    def plot_heading_hist(self):

        with self.ft_manager._ft_buffer_lock:
            headings = self.plot_deques['heading'].vals
            hist, edges, centers = numba_histogram(headings, 20)

        self.heading_hist_plotitem.plot(centers, hist, brush=(0,0,255,150),
                                        fillLevel=0, clear=True, _callSync='off')
        self.heading_hist_plotitem.plot([headings[-1], headings[-1]], [0,.2], pen=(255,0,0), _callSync='off')


    def closeEvent(self, event: QtGui.QCloseEvent):
        '''

        :param event:
        :return:
        '''

        # close fictrac
        self.ft_manager.stop_reading()
        self.ft_manager.close()
        self._pl_serial_thread.join()
        
        # stop scan
        self.stop_scan()
        self._isreading_teensy.clear()

        # join serial processing threads
        self.teensy_read_handle.join()
        self.teensy_input_serial.close()
        
        # close remote plotting processes
        self.cumm_path_plotwidget.close()
        self.heading_hist_plotwidget.close()

        # disconnect shared memory
        for k, v in self.plot_deques.items():
            v.close()
        
        self.disconnect()
        event.accept()
