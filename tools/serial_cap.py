import serial, time, sys
end = time.time() + float(sys.argv[1])
with open(sys.argv[2], 'a') as f:
    while time.time() < end:
        try:
            s = serial.Serial(); s.port='/dev/ttyUSB0'; s.baudrate=115200; s.timeout=0.5
            s.dtr=False; s.rts=False; s.open()
            f.write('--- [logger] port opened %s\n' % time.strftime('%H:%M:%S')); f.flush()
            while time.time() < end:
                line = s.readline()
                if line: f.write(line.decode('utf-8','replace')); f.flush()
        except Exception as e:
            f.write('--- [logger] %s: %s\n' % (time.strftime('%H:%M:%S'), e)); f.flush()
            time.sleep(1)
