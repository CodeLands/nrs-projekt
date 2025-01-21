import serial
import threading
import json
import struct
import time
import tkinter as tk
from tkinter import ttk, scrolledtext
from datetime import datetime
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import matplotlib
matplotlib.use('TkAgg')

class STMMonitor:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("STM Board Monitor")
        self.selected_sensor = tk.StringVar(value="gyro")  # Default to gyroscope
        self.setup_gui()
        self.running = False
        self.serial_port = None
        
        # Buffer sizes for correct frequencies * window size (3s) + 20% margin
        gyro_buffer = int(200 * 3 * 1.2)  # 200Hz * 3s * 1.2 = 720 samples
        acc_buffer = int(50 * 3 * 1.2)    # 50Hz * 3s * 1.2 = 180 samples
        mag_buffer = int(20 * 3 * 1.2)    # 20Hz * 3s * 1.2 = 72 samples
        
        # Initialize deques with calculated buffer sizes
        self.gyro_x_data = deque(maxlen = gyro_buffer)
        self.gyro_y_data = deque(maxlen = gyro_buffer)
        self.gyro_z_data = deque(maxlen = gyro_buffer)
        self.gyro_time = deque(maxlen = gyro_buffer)
        
        self.acc_x_data = deque(maxlen = acc_buffer)
        self.acc_y_data = deque(maxlen = acc_buffer)
        self.acc_z_data = deque(maxlen = acc_buffer)
        self.acc_time = deque(maxlen = acc_buffer)
        
        self.mag_x_data = deque(maxlen = mag_buffer)
        self.mag_y_data = deque(maxlen = mag_buffer)
        self.mag_z_data = deque(maxlen = mag_buffer)
        self.mag_time = deque(maxlen = mag_buffer)
        
        self.plot_active = False
        self.fig = None
        self.ax = None
        self.ani = None
        self.lines = []
        self.last_update = time.time()

    def setup_gui(self):
        control_frame = ttk.Frame(self.root)
        control_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Port and connection controls
        ttk.Label(control_frame, text="Port:").pack(side=tk.LEFT)
        self.port_entry = ttk.Entry(control_frame, width=15)
        self.port_entry.insert(0, "COM4")
        self.port_entry.pack(side=tk.LEFT, padx=5)
        
        self.connect_button = ttk.Button(control_frame, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side=tk.LEFT, padx=5)
        
        self.clear_button = ttk.Button(control_frame, text="Clear", command=self.clear_display)
        self.clear_button.pack(side=tk.LEFT, padx=5)
        
        # Graph controls frame
        graph_control_frame = ttk.LabelFrame(control_frame, text="Select Sensor")
        graph_control_frame.pack(side=tk.LEFT, padx=5)
        
        ttk.Radiobutton(graph_control_frame, text="Gyroscope", value="gyro", 
                       variable=self.selected_sensor).pack(side=tk.LEFT)
        ttk.Radiobutton(graph_control_frame, text="Accelerometer", value="acc", 
                       variable=self.selected_sensor).pack(side=tk.LEFT)
        ttk.Radiobutton(graph_control_frame, text="Magnetometer", value="mag", 
                       variable=self.selected_sensor).pack(side=tk.LEFT)
        
        self.graph_button = ttk.Button(control_frame, text="Toggle Graph", command=self.toggle_graph)
        self.graph_button.pack(side=tk.LEFT, padx=5)
        
        self.debug_text = scrolledtext.ScrolledText(self.root, height=20)
        self.debug_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

    def setup_plot(self):
        # Configure plot based on selected sensor
        sensor = self.selected_sensor.get()
        empty_data = [[], []]
        
        if sensor == "gyro":
            self.ax.set_ylim(-300, 300)
            self.ax.set_title('Gyroscope (DPS)')
        elif sensor == "acc":
            self.ax.set_ylim(-4, 4)
            self.ax.set_title('Accelerometer (g)')
        else:  # mag
            self.ax.set_ylim(-1.5, 1.5)
            self.ax.set_title('Magnetometer (gauss)')
            
        self.ax.grid(True)
        self.ax.set_xlim(0, 3) # 3 second window

        # Lines for each axis
        x_line, = self.ax.plot([], [], '-r', label='X', animated=True)
        y_line, = self.ax.plot([], [], '-g', label='Y', animated=True)
        z_line, = self.ax.plot([], [], '-b', label='Z', animated=True)
        
        self.ax.legend(loc='upper right')
        self.ax.set_xlabel('Time (s)')
        
        # Initialize lines with empty data
        for line in [x_line, y_line, z_line]:
            line.set_data(empty_data[0], empty_data[1])
        
        self.lines = [x_line, y_line, z_line]
        self.fig.tight_layout()

    def toggle_graph(self):
        if not self.plot_active:
            plt.close('all')  # Close any existing windows
            self.plot_active = True

            # Clear all data buffers
            for deq in [self.gyro_x_data, self.gyro_y_data, self.gyro_z_data,
                       self.acc_x_data, self.acc_y_data, self.acc_z_data,
                       self.mag_x_data, self.mag_y_data, self.mag_z_data,
                       self.gyro_time, self.acc_time, self.mag_time]:
                deq.clear()
            
            # Create new plot
            plt.style.use('dark_background')
            self.fig, self.ax = plt.subplots(figsize=(10, 4))
            self.setup_plot()
            self.ani = FuncAnimation(self.fig, self.update_plot, interval=100,
                                   blit=True, cache_frame_data=False)
            plt.show()
        else:
            # Clean up plot resources
            self.plot_active = False
            if self.ani is not None:
                self.ani.event_source.stop()
                self.ani = None
            if self.fig is not None:
                plt.close(self.fig)
                self.fig = None
                self.ax = None
            self.lines = []

    def update_plot(self, frame):
        try:
            current_time = time.time()
            # Throttle updates to reduce CPU usage
            if current_time - self.last_update < 0.1:  # 100ms between updates
                return self.lines

            sensor = self.selected_sensor.get()
            
            # Get the appropriate data based on selected sensor
            if sensor == "gyro" and len(self.gyro_time) > 0:
                time_data = self.gyro_time
                data_x = self.gyro_x_data
                data_y = self.gyro_y_data
                data_z = self.gyro_z_data
            elif sensor == "acc" and len(self.acc_time) > 0:
                time_data = self.acc_time
                data_x = self.acc_x_data
                data_y = self.acc_y_data
                data_z = self.acc_z_data
            elif sensor == "mag" and len(self.mag_time) > 0:
                time_data = self.mag_time
                data_x = self.mag_x_data
                data_y = self.mag_y_data
                data_z = self.mag_z_data
            else:
                return self.lines

            # Calculate relative time points
            relative_times = [t - time_data[0] for t in time_data]
            
            # Update the lines
            self.lines[0].set_data(relative_times, data_x)
            self.lines[1].set_data(relative_times, data_y)
            self.lines[2].set_data(relative_times, data_z)

            # Last 3 seconds of data
            current_time = relative_times[-1]
            if current_time > 3:
                self.ax.set_xlim(current_time - 3, current_time)
            
            self.last_update = time.time()
            
        except (IndexError, ValueError) as e:
            pass
        
        return self.lines

    def handle_binary_data(self, data):
        try:
            # Unpack binary data: header (2 bytes) + packet number and xyz values (8 bytes)
            header = struct.unpack('<H', data[0:2])[0]
            packet, x, y, z = struct.unpack('<Hhhh', data[2:10])
            current_time = time.time() 
            
            # Gyroscope data
            if header == 0xCCCC:
                sensitivity = 500.0 / 32768.0   # Convert raw values to ±500 DPS range (16-bit)
                values = {
                    'x': x * sensitivity,
                    'y': y * sensitivity,
                    'z': z * sensitivity
                }
                self.log_debug(f"GYRO Binary #{packet}: DPS(X={values['x']:.3f}, Y={values['y']:.3f}, Z={values['z']:.3f})")
                
                if self.plot_active:
                    self.gyro_time.append(current_time)
                    self.gyro_x_data.append(values['x'])
                    self.gyro_y_data.append(values['y'])
                    self.gyro_z_data.append(values['z'])

            # Accelerometer data
            elif header == 0xBBBB:  # HEADER_ACC
                sensitivity = 4.0 / 32768.0
                values = {
                    'x': x * sensitivity,
                    'y': y * sensitivity,
                    'z': z * sensitivity
                }
                self.log_debug(f"ACCEL Binary #{packet}: g(X={values['x']:.3f}, Y={values['y']:.3f}, Z={values['z']:.3f})")
                
                if self.plot_active:
                    self.acc_time.append(current_time)
                    self.acc_x_data.append(values['x'])
                    self.acc_y_data.append(values['y'])
                    self.acc_z_data.append(values['z'])

            # Magnetometer data
            elif header == 0xAAAB:
                sensitivity = 50.0 / 32768.0 
                values = {
                    'x': x * sensitivity,
                    'y': y * sensitivity,
                    'z': z * sensitivity
                }
                self.log_debug(f"MAG Binary #{packet}: gauss(X={values['x']:.3f}, Y={values['y']:.3f}, Z={values['z']:.3f})")
                
                if self.plot_active:
                    self.mag_time.append(current_time)
                    self.mag_x_data.append(values['x'])
                    self.mag_y_data.append(values['y'])
                    self.mag_z_data.append(values['z'])

        except struct.error as e:
            self.log_debug(f"Binary parse error: {str(e)}")

    def read_serial(self):
        buffer = bytearray()
        while self.running:
            if self.serial_port.in_waiting:
                byte = self.serial_port.read()
                buffer.extend(byte)
                
                # Check for complete binary packet (10 bytes)
                if len(buffer) >= 10:
                    header = (buffer[1] << 8) | buffer[0]
                    if header in [0xCCCC, 0xBBBB, 0xAAAB]:
                        self.handle_binary_data(buffer[:10])
                        buffer = buffer[10:]
                        continue

                # Handle any text/JSON data that might be in the buffer
                try:
                    if b'\n' in buffer:
                        line = buffer[:buffer.index(b'\n')].decode('utf-8')
                        buffer = buffer[buffer.index(b'\n')+1:]
                        if line.startswith('{') and line.endswith('}'):
                            self.handle_json_sensor_data(line)
                        else:
                            self.log_debug(line)
                except UnicodeDecodeError:
                    buffer = buffer[1:]

    def toggle_connection(self):
        if not self.running:
            try:
                port = self.port_entry.get()
                self.serial_port = serial.Serial(port, 115200, timeout=0.1)
                self.running = True
                self.connect_button.config(text="Disconnect")
                threading.Thread(target=self.read_serial, daemon=True).start()
                self.log_debug("Connected to " + port)
            except Exception as e:
                self.log_debug(f"Connection error: {str(e)}")
        else:
            self.running = False
            if self.serial_port:
                self.serial_port.close()
            self.connect_button.config(text="Connect")
            self.log_debug("Disconnected")

    def handle_json_sensor_data(self, data):
        try:
            parsed = json.loads(data)
            sensor_type = next(iter(parsed)) # Get first key from JSON object
            message = f"{sensor_type}: X={parsed['X']:.3f}, Y={parsed['Y']:.3f}, Z={parsed['Z']:.3f}"
            self.log_debug(message)
        except json.JSONDecodeError as e:
            self.log_debug(f"JSON parse error: {str(e)}")

    def log_debug(self, message):
        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        self.debug_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.debug_text.see(tk.END) # Scroll to the bottom

    def clear_display(self):
        self.debug_text.delete(1.0, tk.END)

    def run(self):
        self.root.mainloop()

if __name__ == '__main__':
    monitor = STMMonitor()
    monitor.run()