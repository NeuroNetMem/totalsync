import base64
from pathlib import Path
from datetime import datetime
import logging
import tkinter as tk
from tkinter import filedialog


def browse_button():
    # Allow user to select a directory and store it in global var
    # called folder_path
    global folder_path
    filename = filedialog.askdirectory()
    folder_path.set(filename)
    if filename == "":
        filename = "DataFile/"

    return filename


root2 = tk.Tk()
folder_path = tk.StringVar()
lbl1 = tk.Label(master=root2, textvariable=folder_path)
lbl1.grid(row=0, column=1)
button2 = tk.Button(master=root2, text="Browse", command=browse_button)
button2.grid(row=0, column=3)
root2.destroy()


class SerialDump:
    def __init__(self):

        self.f_b64 = Path(browse_button() + '/' + datetime.now().strftime("%Y%m%d-%H%M%S_%f")[:-3] + '.b64')
        self.f_bin = self.f_b64.with_suffix('.bin')

        self.cobs_file = None
        self.bin_file = None

    def handle_raw(self, arr):
        """Write the cobs-encoded, 0-terminated data as base64 encoded string to log file,
        one line per packet.

        To allow re-parsing of the array with a cobs-decoder, we re-add the 0 terminator that was
        removed by the serial packetizer. (This is completely unnecessary and here for backwards
        compatibility).
        """
        # For future parsing, we need to re-append the line termination symbol \0.
        if self.cobs_file is None:
            logging.info(f"Opening COBS+Base64 serial dump file {self.f_b64.absolute()}")
            self.cobs_file = open(self.f_b64, 'w+b')
        self.cobs_file.write(base64.b64encode(arr + b'\0') + b'\n')

    def handle_array(self, arr):
        """Write the cobs-decoded byte-array packet into a .bin file.

        This allows use of a mmap to interpret the file without having to decode again."""
        if self.bin_file is None:
            logging.info(f"Opening binary serial dump file {self.f_bin.absolute()}")
            self.bin_file = open(self.f_bin, 'w+b')
        self.bin_file.write(arr)

    def __del__(self):
        self.cobs_file.close()
        self.bin_file.close()

