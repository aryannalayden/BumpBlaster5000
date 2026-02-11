import os
import socket
import warnings
import select
import subprocess
import threading
import queue
import multiprocessing as mp
from glob import glob
import time
from collections import deque

import pandas as pd

from ..utils import threaded
from .. import shared_memory, params




FICTRAC_PATH = r'C:\Users\fisherlab\Documents\FicTrac 2.1.1\fictrac.exe'
CONFIG_PATH = r'C:\Users\fisherlab\Documents\FicTrac 2.1.1\config.txt'

class FicTracSubProcess:
    '''

    '''

    def __init__(self, fictrac_path=FICTRAC_PATH, config_file=CONFIG_PATH):
        self.fictrac_path = fictrac_path
        self.config_file = config_file
        self.p = None
        self.open_evnt = threading.Event()

    def open(self, creationflags=subprocess.CREATE_NEW_CONSOLE):
        '''

        :param creationflags:
        :return:
        '''
        self.p = subprocess.Popen([self.fictrac_path, self.config_file], creationflags=creationflags)
        self.open_evnt.set()

    def close(self):
        '''

        :return:
        '''
        self.p.kill()
        self.p.terminate()
        self.p = None
        self.open_evnt.clear()

        

class FicTracSocketManager:
    """


    """

    def __init__(self, fictrac_path=FICTRAC_PATH, config_file=CONFIG_PATH, host='127.0.0.1', port=65413,
                 columns_to_read=None, plot_buffer_time = 600,
                 ):
        """

        :param fictrac_path:
        :param config_file:
        :param host:
        :param port:
        :param columns_to_read:
        """

        if columns_to_read is None:
            columns_to_read = {'heading': 17, 'integrated x': 15, 'integrated y': 16, 'speed': 19}
        self.ft_subprocess = FicTracSubProcess(fictrac_path=fictrac_path,
                                               config_file=config_file)

        self.host = host
        self.port = port
        self.reading = threading.Event()
        self._reading_thread_handle = None
        self._sock = None
        self._socket_open = threading.Event()
        self.open_socket()

        self.ft_timeout = 1
        self._ft_buffer_lock = threading.Lock()
        self.ft_buffer = ""
        self.ft_output_path = None
        self._ft_output_handle = None
       
        self.ft_serial_queue = queue.SimpleQueue()
        
        self.columns_to_read = columns_to_read
        #ToDo: downsample this and make it into a shared memory object for faster plotting
        self._frame_counter = 0
        self._ds = 10
        self.plot_deques = {k: shared_memory.CircularFlatBuffer(params.FT_PC_PARAMS['plot_buffer_length'], name = k).create() for k in columns_to_read.keys()}

        
        

        

    def open(self, timeout = 5, output_path=None):
        '''

        :return:
        '''

        if output_path is not None:
            output_dir, _ = os.path.split(output_path)
            os.chdir(output_dir)

            self.ft_output_path = output_path
            self._ft_output_handle = open(output_path,'wb')


        self.ft_subprocess.open()
        tic = time.perf_counter()
        print('Waiting for FicTrac to finish openiing')
        while not self.ft_subprocess.open_evnt.is_set():
            if time.perf_counter() - tic < timeout:
                time.sleep(.01)
            else:
                warnings.warn('Timeout exceeded. Fictrac may not be open')
                break



        if not self._socket_open.is_set():
            self.open_socket()


    def start_reading(self):
        """

        :param fictrac_output_file:
        :return:
        """
        # check if output file exists
        


        

        # open output file
        self.reading.set()
        self._reading_thread_handle = self._read_thread()

    def stop_reading(self, return_pandas=False):
        """

        :return:
        """

        self.reading.clear()
        self._reading_thread_handle.join()
        self._reading_thread_handle = None
        self._ft_output_handle.close()

    
    def open_socket(self):
        """

        :return:
        """
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((self.host, self.port))
        self._sock.setblocking(False)
        self._socket_open.set()


    def close_socket(self):
        """

        :return:
        """
        self._sock.close()
        self._socket_open.clear()

    def close(self):
        """

        :return:
        """

        if self.reading.is_set():
            self.stop_reading()

        if self._socket_open.is_set():
            self.close_socket()

        if self.ft_subprocess.open_evnt.is_set():
            self.ft_subprocess.close()

        if isinstance(self._reading_thread_handle, threading.Thread):
            self._reading_thread_handle.join()
            self._reading_thread_handle = None

        # time.sleep(.1)
        # _ = [os.remove(_f) for _f in glob(os.path.join(os.getcwd(),"fictrac-*.log"))]
        # _ = [os.remove(_f) for _f in glob(os.path.join(os.getcwd(),"fictrac-*.dat"))]


    # def read_ft_queue(self):
    #     """

    #     :return:
    #     """
    #     try:
    #         return self.ft_queue.get()
    #     except queue.Empty:
    #         return None



    @threaded
    def _read_thread(self):
        '''

        :return:
        '''

        while self.reading.is_set():
            # Check to see whether there is data waiting
            ready = select.select([self._sock], [], [], self.ft_timeout)

            # Only try to receive data if there is data waiting
            if ready[0]:
                single_line = self._process_line()
                # maybe want to replace queue with just a locked value to speed up plotting
                # self.ft_queue.put(single_line)
                
            else:
                pass

    def _process_line(self):
        '''

        :return:
        '''
        # Receive one data frame
        new_data = self._sock.recv(4096)  # new_data = 0 if no bytes sent
        if not new_data:
            return

        # Decode received data
        with self._ft_buffer_lock:
            self.ft_buffer += new_data.decode('UTF-8')

            # Find the first frame of data
            endline = self.ft_buffer.find("\n")
            line = self.ft_buffer[:endline]  # copy first frame

            # Tokenise
            toks = line.split(", ")

            # Check that we have sensible tokens
            if ((len(toks) < 24) | (toks[0] != "FT")):
                print('Bad read')
                return

            # print to output file
            #TODO: put this back in after dealing with filenames
            self._ft_output_handle.writelines([self.ft_buffer.encode('UTF-8'),])
            self.ft_buffer = self.ft_buffer[endline + 1:]  # delete first frame

        # extract fictrac variables
        # (see https://github.com/rjdmoore/fictrac/blob/master/doc/data_header.txt for descriptions)
        self._frame_counter = (self._frame_counter+1) % self._ds
        if self._frame_counter == 0:
            with self._ft_buffer_lock:
                for k,v in self.columns_to_read.items():
                    self.plot_deques[k].append(float(toks[v]))

        #TODO: write to serial queue
        self.ft_serial_queue.put(toks[self.columns_to_read['heading']])

            
        return {k: toks[v] for k, v in self.columns_to_read.items()}

    def reset_plot_dequeus(self):

        with self._ft_buffer_lock:
            for k,v in self.columns_to_read.items():
                self.plot_deques[k].reset()
                
        return