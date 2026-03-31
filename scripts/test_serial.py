import serial
import time

s = serial.Serial('COM13', 115200)
time.sleep(2)

print("Sending 4095")
s.write(b'1,7,4095\n')
time.sleep(1)

print("Sending 0")
s.write(b'1,7,0\n')

s.close()
print("Done")