import argparse
import logging
import sys
import threading
import time
import tkinter

import serial
import serial.threaded
import serial.tools.list_ports
import zmq

try:
    import curses
except ImportError:
    curses = None

from webinterface.totalsync.Packet import PacketReceiver, pack_command_packet, pack_reset_packet
from webinterface.totalsync.SerialDump import SerialDump
from webinterface.totalsync.SerialDummy import SerialDummy
from webinterface.totalsync.WebInterface import WebInterface, WS_PORT, HTTP_PORT
from webinterface.totalsync.CursesInterface import CursesUI

import tkinter as tk
from tkinter import messagebox as mb
import webbrowser

USER_INP = ""


def answer():
    url = "http://localhost:63342/Packet.py/webinterface/web/index.html?_ijt=8ei5l38phh4ebi472rogof72uf&_ij_reload=RELOAD_ON_SAVE"
    webbrowser.open_new(url)
    root.attributes('-topmost', 1)
    root.destroy()


def callback():
    if mb.askyesno('Verify', 'Really quit?'):
        root.destroy()
        sys.exit(0)
    else:
        mb.showinfo('No', 'Quit has been cancelled')


root = tk.Tk()


def on_closing():
    if mb.askokcancel("Quit", "Do you want to quit?"):
        root.destroy()
        sys.exit(0)


def com():
    user_inp = tk.Tk()
    user_inp.title("Select the COM port you want to use ")
    user_inp.geometry('500x20')
    value = tk.StringVar(user_inp)

    options_list = sorted([comport.device for comport in serial.tools.list_ports.comports()])

    value.set('Select')

    def choice(val):
        global USER_INP
        USER_INP = val
        print(USER_INP)
        if USER_INP == '':
            USER_INP = 'COM9' #13
        if USER_INP is None:
            USER_INP = 'COM9' #13
        user_inp.destroy()

    question = tk.OptionMenu(user_inp, value, *options_list, command=choice)
    question.pack()
    user_inp.mainloop()


root.protocol("WM_DELETE_WINDOW", on_closing)
root.title('Totalsync')
root.geometry('400x150')

lbl1 = tk.Label(master=root,
                text="Welcome to TotalSync")
lbl1.pack()

button1 = tk.Button(text='Quit', command=callback)
button1.pack()
button2 = tk.Button(text='Play', command=answer)
button2.pack()

button4 = tk.Button(text='choose COM', command=com)
button4.pack()
tk.mainloop()

ROOT3 = tk.Tk()
ROOT3.withdraw()

# the input dialog
if USER_INP == "":
    USER_INP = "COM9" #13

SERIAL_PORT = USER_INP

ZMQ_SERVER_PUB_PORT = 5680
ZMQ_SERVER_SUB_PORT = 5681


class TeensyCommander:
    def __init__(self, serial_port, http_port, ws_port, curses_screen, write_bin=False, use_dummy=False):
        self.n_packet = 0
        self.packets_per_second = 0
        self.packet_timings = []
        self.zmq_ctx = zmq.Context()
        self.zmq_pub = self.zmq_ctx.socket(zmq.PUB)
        self.zmq_pub.bind(f'tcp://*:{ZMQ_SERVER_PUB_PORT}')

        self.zmq_sub = self.zmq_ctx.socket(zmq.SUB)
        self.zmq_sub.setsockopt_string(zmq.SUBSCRIBE, "")
        self.zmq_sub.bind(f'tcp://*:{ZMQ_SERVER_SUB_PORT}')

        self.alive = True
        self.shell_gui = CursesUI(self, curses_screen) if curses_screen is not None else None
        time.sleep(0.05)  # give some time to let log display catch all startup messages

        self.serial_dump = SerialDump()
        self.serial_port = serial_port
        self.web_server = WebInterface(http_port, ws_port, self)

        try:
            self.serial = serial.Serial(self.serial_port)
            self.serial.flushInput()
        except serial.SerialException as e:
            logging.error("Can't find serial device: {}".format(e))
            if use_dummy:
                logging.warning('Using serial dummy')
                self.dummy = SerialDummy()
                self.serial = self.dummy.ser
                self.serial_port = 'DUMMY'
            else:
                exit()

        self.serial_reader = serial.threaded.ReaderThread(self.serial, PacketReceiver).__enter__()

        # raw data consumers
        self.serial_reader.raw_callbacks.append(self.serial_dump.handle_raw)

        # decoded data consumers
        if write_bin:
            self.serial_reader.raw_callbacks.append(self.serial_dump.handle_array)
        # unpacked data consumers
        self.serial_reader.packet_callbacks.append(self.handle_packet)
        self.serial_reader.packet_callbacks.append(self.web_server.handle_packet)
        if self.shell_gui:
            self.serial_reader.packet_callbacks.append(self.shell_gui.handle_packet)

        self.zmq_subscriber = threading.Thread(target=self.subscriber, daemon=True)

    def run_forever(self):
        self.zmq_subscriber.start()
        while self.alive:
            if self.shell_gui and self.shell_gui.alive and not self.shell_gui.is_alive():
                logging.critical("Shell GUI died!")
                # self.alive = False
                # TODO: attempt to restart the shell GUI
            time.sleep(1)

    def handle_packet(self, packet):
        self.n_packet += 1
        self.zmq_pub.send_pyobj(packet)

        # calculate packet rate
        t_now = time.time_ns() * 0.000000001
        self.packet_timings.append(t_now)
        t_delta = t_now - (self.packet_timings[0] if len(self.packet_timings) < 1000 else self.packet_timings.pop(0))
        try:
            self.packets_per_second = len(self.packet_timings) / t_delta
        except ZeroDivisionError:
            self.packets_per_second = 0

    def subscriber(self):
        while True:
            try:
                msg = self.zmq_sub.recv_pyobj()
            except zmq.ZMQBaseError as e:
                if e.errno == zmq.ETERM:
                    break
                else:
                    raise
            if msg:
                self.send(msg)

    def send(self, msg):
        if msg is not None:
            logging.debug('Send message: ' + str(msg))
            self.pack_packet(msg)
        else:
            logging.error('Asked to send "None" message, skipping.')

    def pack_packet(self, instruction):
        logging.debug(f'Packing instruction: {instruction}')
        try:
            packed = pack_command_packet(instruction)
            if packed is None:
                logging.error('Failed to pack!')
                return
            self.send_packet(packed)
        except ValueError:
            logging.debug('Unknown type!')

    def reset_packet(self):
        logging.debug('Reset')
        try:
            reset = pack_reset_packet()
            SerialDump()
            if reset is None:
                logging.error('Failed to pack!')
                return
            self.send_packet(reset)
        except ValueError:
            logging.debug('Unknown type!')

    def send_packet(self, packet):
        logging.debug('Serial write: ' + str(packet))
        try:
            self.serial.write(packet)
        except (ValueError, serial.SerialException) as e:
            logging.error(f'Serial write failed: {e}')

    def __del__(self):
        logging.debug('Making sure serial port is closed on exit.')
        if self.serial and self.serial.isOpen():
            self.serial.close()


def main(screen, cli_args):
    logging.info(
        "Known serial ports: " + repr(sorted([comport.device for comport in serial.tools.list_ports.comports()])))
    logging.info(
        f"Launching Teensy Commander on serial port {cli_args.serial_port} and the web interface "
        f"on ports HTTP:{cli_args.http_port} and WS:{cli_args.ws_port}")
    tc = TeensyCommander(serial_port=cli_args.serial_port,
                         http_port=cli_args.http_port,
                         ws_port=cli_args.ws_port,
                         curses_screen=screen,
                         write_bin=cli_args.binfile,
                         use_dummy=cli_args.dummy)
    menu(tc)
    try:
        tc.run_forever()
    except KeyboardInterrupt:
        if tc.alive:
            tc.alive = False
        else:
            sys.exit(0)


def cli_entry():
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--serial_port', default=SERIAL_PORT)
    parser.add_argument('-w', '--ws_port', default=WS_PORT)
    parser.add_argument('-H', '--http_port', default=HTTP_PORT)
    parser.add_argument('-B', '--binfile', action='store_true', help='Write decoded binary serial dump file')
    parser.add_argument('-C', '--curses', action='store_true', help='Use cursesUI in terminal')
    parser.add_argument('-D', '--dummy', action='store_true',
                        help='Use serial dummy if no valid serial device available')
    parser.add_argument('-v', '--verbose', action='count', default=2, help="Increase logging verbosity")

    cli_args = parser.parse_args()

    try:
        loglevel = {
            0: logging.ERROR,
            1: logging.WARN,
            2: logging.INFO,
        }[cli_args.verbose]
    except KeyError:
        loglevel = logging.DEBUG

    log_format = '[%(asctime)s]{%(filename)s:%(lineno)d} %(levelname)s - %(message)s'
    logging.basicConfig(level=loglevel,
                        format=log_format,
                        datefmt='%H:%M:%S')

    console = logging.StreamHandler()
    console.setLevel(loglevel)
    console.setFormatter(logging.Formatter(log_format))

    # start_time_str = datetime.now().strftime("%Y%m%d-%H%M%S")
    # log_file = logging.FileHandler('../totalsync/LogFile/' + f'{start_time_str}_teensy_commander.log', mode='w')
    # log_file.setLevel(logging.DEBUG)
    # log_file.setFormatter(logging.Formatter(log_format))

    logging.getLogger().handlers.clear()
    logging.getLogger().addHandler(console)
    # logging.getLogger().addHandler(log_file)
    logging.getLogger().setLevel(loglevel)

    if cli_args.curses and curses is not None:
        curses.wrapper(main, cli_args)
    else:
        main(None, cli_args)


def menu(TeensyCommander):
    root4 = tk.Tk()

    def callback():
        if mb.askyesno('Verify', 'Really quit?'):
            root4.destroy()
            exit()
        else:
            mb.showinfo('No', 'Quit has been cancelled')

    def Reset(Teensy):
        Teensy.reset_packet()
        url = "http://localhost:63342/Packet.py/webinterface/web/index.html"
        webbrowser.open_new(url)

    def Reload():

        url = "http://localhost:63342/Packet.py/webinterface/web/index.html"
        webbrowser.open_new(url)

    def on_closing():
        if mb.askokcancel("Quit", "Do you want to quit?"):
            root4.destroy()
            sys.exit(0)

    root4.protocol("WM_DELETE_WINDOW", on_closing)
    lbl1 = tk.Label(master=root4,
                    text="TotalSync is now in use")
    lbl1.pack()
    button1 = tk.Button(master=root4, text='Quit', command=callback)
    button1.pack()
    button3 = tk.Button(master=root4, text='Reset', command=lambda: Reset(TeensyCommander))
    button3.pack()
    button4 = tk.Button(master=root4, text='Reload', command=Reload)
    button4.pack()

    tk.mainloop()


if __name__ == "__main__":
    cli_entry()
