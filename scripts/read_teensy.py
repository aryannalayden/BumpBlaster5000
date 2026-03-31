import serial, time, sys, threading

port_in = 'COM10'
port_out = 'COM10'
baud = 115200

# Try to open input port
try:
    s_in = serial.Serial(port_in, baud, timeout=1)
    print(f'✓ Opened {port_in} (input)')
except Exception as e:
    print(f'✗ Failed to open {port_in}: {e}')
    sys.exit(2)

# Try to open output port
s_out = None
try:
    s_out = serial.Serial(port_out, baud, timeout=1)
    print(f'✓ Opened {port_out} (output)')
except Exception as e:
    print(f'⚠ Could not open {port_out} (expected if single USB CDC): {e}')

time.sleep(0.5)

# Send test commands
commands = [
    (b'2,1\n', 'Start scan'),
    (b'2,3\n', 'Trigger opto'),
    (b'2,2\n', 'Stop scan'),
]

for cmd, desc in commands:
    print(f'\n→ Sending {desc}: {cmd}')
    s_in.write(cmd)
    time.sleep(0.2)
    
    # Try to read from input port
    line = s_in.readline()
    if line:
        print(f'  RX (input): {line.decode("utf-8", errors="replace").rstrip()}')
    
    # Try to read from output port if available
    if s_out:
        line = s_out.readline()
        if line:
            print(f'  RX (output): {line.decode("utf-8", errors="replace").rstrip()}')

# Clean read for a few seconds
print(f'\nListening for any unsolicited output (5s)...')
end = time.time() + 5
while time.time() < end:
    if s_in.inWaiting():
        line = s_in.readline()
        if line:
            print(f'  RX (input): {line.decode("utf-8", errors="replace").rstrip()}')
    if s_out and s_out.inWaiting():
        line = s_out.readline()
        if line:
            print(f'  RX (output): {line.decode("utf-8", errors="replace").rstrip()}')
    time.sleep(0.01)

s_in.close()
if s_out:
    s_out.close()
print('\nDone.')

