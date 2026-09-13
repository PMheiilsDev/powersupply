from serial import Serial

import time
import json
from random import randint

def document_jsonl(file_path, success, target, data, delay_pre_query, delay_post_query):
    d = {"success": success, "target": target, "data": data, "delay_pre_query": delay_pre_query, "delay_post_query": delay_post_query}
    with open(file_path, "a") as f:
        f.write(json.dumps(d) + "\n")

def write_voltage_command(ser, voltage, printer=None):
    command = f"VOLTAGE {voltage}\n"
    if printer:
        printer(f"Sending command: {command}")
    ser.write(command.encode())
    time.sleep(0.1)  # Wait for the device to process the command
    
def query_voltage(ser, printer=None):
    ser.read_all()
    ser.write("VOLTAGE?\n".encode())
    response = ser.readline()
    if printer:
        printer(f"Received response: {response.decode().strip()}")
    return response.decode().strip()

if __name__ == "__main__":
    with Serial(port="/dev/ttyACM0", baudrate=115200, timeout=1) as ser:
        
        for pre_delay in [.1, .25, .5, 1.0]:
            for post_delay in [.1, .25, .5, 1.0]:
                for _ in range(10):
                
                    # Send a command to the device
                    voltage = f"{randint(5, 10)}.{randint(0, 99)}"
                    write_voltage_command(ser, voltage)

                    print(f"Sent voltage command: {voltage}")
                    
                    time.sleep(pre_delay)  # Wait for the device to process the command
                    
                    # Query the device for the current voltage
                    current_voltage = query_voltage(ser,printer=print)
                    # print(f"Current voltage reported by device: {current_voltage}")
                    
                    try:
                        if float(current_voltage) != float(voltage):
                                document_jsonl("voltage_commands.jsonl", success=False, target=voltage, data=current_voltage, delay_pre_query=pre_delay, delay_post_query=post_delay)
                        
                        else:
                            document_jsonl("voltage_commands.jsonl", success=True, target=voltage, data=current_voltage, delay_pre_query=pre_delay, delay_post_query=post_delay)
                    except ValueError:
                        document_jsonl("voltage_commands.jsonl", success=False, target=voltage, data=current_voltage, delay_pre_query=pre_delay, delay_post_query=post_delay)
                        
                    time.sleep(post_delay)  # Wait before sending the next command