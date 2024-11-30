import serial
import struct
import threading
import csv
from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg
from PyQt5.QtCore import QTimer

# Serial port settings
SERIAL_PORT = 'COM17'
BAUD_RATE = 230400
TIMEOUT = 1

# Recording flag
record = False

# Global variables
sample_index = 0
recorded_data = []
markers = {}  # Changed from list to dict for faster lookup

# Sequence list for labels
sequencelist = [
    "735S1A0L", "735S1A1L", "735S1A2S", "735S1A4L", "735S1A5L",
    "850S1A0L", "850S1A1L", "850S1A2S", "850S1A4L", "850S1A5L",
    "735S2A2L", "735S2A3L", "735S2A4S", "735S2B0L", "735S2B1L",
    "850S2A2L", "850S2A3L", "850S2A4S", "850S2B0L", "850S2B1L",
    "735S3A4L", "735S3A5L", "735S3B0S", "735S3B2L", "735S3B3L",
    "850S3A4L", "850S3A5L", "850S3B0S", "850S3B2L", "850S3B3L",
    "735S4B0L", "735S4B1L", "735S4B2S", "735S4B4L", "735S4B5L",
    "850S4B0L", "850S4B1L", "850S4B2S", "850S4B4L", "850S4B5L"
]

# Packet settings
HEADER_SIZE = 4
FOOTER_SIZE = 4
DATA_SIZE = 80  # 40 samples * 2 bytes each
PACKET_SIZE = HEADER_SIZE + DATA_SIZE + FOOTER_SIZE

# Create packet header and footer
PACKET_HEADER = struct.pack('<I', 0xFFFFFFFF)
PACKET_FOOTER = struct.pack('<I', 0xDEADBEEF)

# Open serial port
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)

# Data buffers and lock
data_buffers = [[] for _ in range(40)]
data_lock = threading.Lock()
serial_buffer = bytearray()

def read_serial():
    global data_buffers, serial_buffer, sample_index, record, recorded_data
    while True:
        try:
            # Read incoming data
            data = ser.read(ser.in_waiting or 1)
            if data:
                serial_buffer.extend(data)
                while True:
                    header_index = serial_buffer.find(PACKET_HEADER)
                    if header_index == -1:
                        if len(serial_buffer) > (HEADER_SIZE - 1):
                            serial_buffer = serial_buffer[-(HEADER_SIZE - 1):]
                        break
                    else:
                        if len(serial_buffer) >= header_index + PACKET_SIZE:
                            packet = serial_buffer[header_index:header_index + PACKET_SIZE]
                            serial_buffer = serial_buffer[header_index + PACKET_SIZE:]
                            if packet[-FOOTER_SIZE:] == PACKET_FOOTER:
                                adc_data = packet[HEADER_SIZE:-FOOTER_SIZE]
                                samples = struct.unpack('<40H', adc_data)
                                with data_lock:
                                    # Record the data point with the current sample_index
                                    marker_label = markers.get(sample_index, '')
                                    data_point = {
                                        'sample_index': sample_index,
                                        'channels': list(samples),
                                        'marker': marker_label
                                    }
                                    if record:
                                        recorded_data.append(data_point)
                                    # Update data buffers
                                    for ch in range(40):
                                        data_buffers[ch].append(samples[ch])
                                        # Limit buffer size to last 100 samples
                                        if len(data_buffers[ch]) > 100:
                                            data_buffers[ch].pop(0)
                                    # Increment sample_index after recording
                                    sample_index += 1
                            else:
                                serial_buffer = serial_buffer[header_index + 1:]
                                break
                        else:
                            if header_index > 0:
                                serial_buffer = serial_buffer[header_index:]
                            break
        except serial.SerialException as e:
            print(f"Serial exception: {e}")
            break

# Start the serial reading thread
serial_thread = threading.Thread(target=read_serial)
serial_thread.daemon = True
serial_thread.start()

# PyQt application setup
app = QtWidgets.QApplication([])

# Create main window widget
main_widget = QtWidgets.QWidget()
main_layout = QtWidgets.QVBoxLayout(main_widget)

# Create control panel
control_layout = QtWidgets.QHBoxLayout()

# Create buttons and text box
record_button = QtWidgets.QPushButton('Record')
marker_button = QtWidgets.QPushButton('Marker')
save_button = QtWidgets.QPushButton('Save')
marker_label_input = QtWidgets.QLineEdit()
marker_label_input.setPlaceholderText('Marker Label')

# Add widgets to control layout
control_layout.addWidget(record_button)
control_layout.addWidget(marker_button)
control_layout.addWidget(marker_label_input)
control_layout.addWidget(save_button)

# Add control layout to main layout
main_layout.addLayout(control_layout)

# Create plot widget
win = pg.GraphicsLayoutWidget()
win.resize(1000, 600)
win.setWindowTitle('Live ADC Data from STM32H723ZG')

# Add plot widget to main layout
main_layout.addWidget(win)

# Show the main widget
main_widget.show()

plots = []
curves = []

# Create pairs of channels
pairs = []
for i in range(0, 40, 10):
    for j in range(5):
        idx1 = i + j
        idx2 = i + j + 5
        pairs.append((idx1, idx2))

# Create plots for each pair
for idx, (ch1, ch2) in enumerate(pairs):
    label = sequencelist[ch1].replace('735', '')
    plot = win.addPlot(row=idx // 5, col=idx % 5, title=label)
    plot.setYRange(0, 65535)
    curve1 = plot.plot(pen='g')  # Green for 735 nm
    curve2 = plot.plot(pen='r')  # Red for 850 nm
    plots.append(plot)
    curves.append([curve1, curve2])

def update():
    with data_lock:
        for idx, (ch1, ch2) in enumerate(pairs):
            ydata1 = data_buffers[ch1][-50:]
            ydata2 = data_buffers[ch2][-50:]
            xdata = list(range(len(ydata1)))
            curves[idx][0].setData(x=xdata, y=ydata1)
            curves[idx][1].setData(x=xdata, y=ydata2)
            # Optionally, display markers (not implemented here)

# Timer for live updates
timer = QTimer()
timer.timeout.connect(update)
timer.start(100)

# Functions for button actions
def toggle_record():
    global record, recorded_data
    if record:
        record = False
        record_button.setText('Record')
    else:
        record = True
        record_button.setText('Stop Recording')
        recorded_data = []  # Clear previous data

def add_marker():
    global markers, sample_index
    label = marker_label_input.text()
    if label == '':
        label = 'Marker'
    with data_lock:
        # Assign marker to the current sample_index
        current_sample_index = sample_index
    markers[current_sample_index] = label  # Store marker with sample index
    print(f"Marker added at sample {current_sample_index} with label '{label}'")

def save_data():
    global recorded_data, markers
    if not recorded_data:
        QtWidgets.QMessageBox.warning(None, 'No Data', 'No data to save. Please start recording first.')
        return
    filename, _ = QtWidgets.QFileDialog.getSaveFileName(None, 'Save Data', '', 'CSV Files (*.csv)')
    if filename:
        try:
            with open(filename, 'w', newline='') as csvfile:
                writer = csv.writer(csvfile)
                # Write header
                header = ["SampleIndex"] + [f"Channel {sequencelist[i]}" for i in range(40)] + ["MarkerIndex", "MarkerLabel"]
                writer.writerow(header)
                # Write data
                for data_point in recorded_data:
                    if data_point['marker']:
                        row = [data_point['sample_index']] + data_point['channels'] + [data_point['sample_index'], data_point['marker']]
                    else:
                        row = [data_point['sample_index']] + data_point['channels'] + ["", ""]
                    writer.writerow(row)
            print(f"Data saved to {filename}")
            QtWidgets.QMessageBox.information(None, 'Save Successful', f'Data saved to {filename}')
        except Exception as e:
            print(f"Error saving data: {e}")
            QtWidgets.QMessageBox.critical(None, 'Save Failed', f'Failed to save data: {e}')

# Connect buttons to functions
record_button.clicked.connect(toggle_record)
marker_button.clicked.connect(add_marker)
save_button.clicked.connect(save_data)

# Start the application
app.exec_()

# Clean up on exit
ser.close()
