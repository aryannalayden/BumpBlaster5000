import numpy as np
from time import sleep
from datetime import datetime



def build_cmd_str(queue):

    


    

    

    queue.put('1,7,4095\n'.encode('UTF-8'))
    sleep(60*5)
    queue.put('1,7,0\n'.encode('UTF-8'))

    # multiline_cmd = '0, 4 \n' + cmd_str + '0, 5 \n'
    return # multiline_cmd.encode('UTF-8')
    
    
def run(queue):
    # go to open loop
    
    # send remapping commands
    # queue.put(build_cmd_str())

    # back to closed loop
    build_cmd_str(queue)

    return
    

