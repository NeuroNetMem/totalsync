import argparse
import logging
import sys
import threading
import time
import tkinter as tk
import webbrowser
from tkinter import messagebox as mb

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

ZMQ_SERVER_PUB_PORT = 5680
ZMQ_SERVER_SUB_PORT = 5681

# The web interface is served by our own HTTP server, see WebInterface.WEB_DIRECTORY.
WEB_URL = f'http://localhost:{HTTP_PORT}/index.html'

# Fallback when neither --serial_port nor the startup dialog supplies a port. There is
# no useful cross-platform default: on POSIX the devices are /dev/cu.* or /dev/ttyUSB*
# and differ per machine, so fall back to an empty string and let the SerialException
# handler in TeensyCommander report it (or use -D to switch to the dummy).
DEFAULT_SERIAL_PORT = 'COM9' if sys.platform == 'win32' else ''

_root = None


def get_root():
    """Return the process-wide hidden Tk root, creating it on first use.

    Tk supports exactly one root per process. Additional tk.Tk() instances are
    separate Tcl interpreters that cannot share variables or `after` callbacks,
    and destroying one on macOS leaves a dangling Aqua idle handler that
    segfaults the next event loop. Every window below is a Toplevel of this root.
    """
    global _root
    if _root is None:
        _root = tk.Tk()
        _root.withdraw()
    return _root


def destroy_root():
    """Tear down the Tk root so the process can exit without a stuck NSApplication."""
    global _root
    if _root is None:
        return
    try:
        # update() first: it drains the idle queue. On macOS a Tk idle handler
        # left unserviced across a destroy() segfaults the next event loop.
        _root.update()
        _root.destroy()
    except tk.TclError:
        pass
    _root = None


# sys.exit() must never be called from inside a Tk callback: the SystemExit it
# raises is discarded by `tkwait` (unlike mainloop(), Tkapp_Call does not restore
# the pending exception), so the caller silently carries on. The dialogs below
# therefore report "the user asked to quit" through their return value instead.
QUIT = object()


def choose_serial_port(parent):
    """Modal dialog to pick a serial port. Returns the device, or '' if cancelled."""
    ports = sorted(comport.device for comport in serial.tools.list_ports.comports())
    if not ports:
        mb.showwarning('No serial ports', 'No serial ports were found on this machine.',
                       parent=parent)
        return ''

    win = tk.Toplevel(parent)
    win.title('Select the serial port you want to use')
    win.geometry('500x40')
    win.transient(parent)

    selected = tk.StringVar(win)
    chosen = []

    def choice(value):
        chosen.append(value)
        win.destroy()

    tk.OptionMenu(win, selected, *ports, command=choice).pack()
    win.grab_set()
    win.wait_window()
    return chosen[0] if chosen else ''


def welcome_dialog():
    """Show the startup window.

    Returns the serial port the user picked ('' if none), or QUIT if the user
    asked to quit.
    """
    win = tk.Toplevel(get_root())
    win.title('Totalsync')
    win.geometry('400x150')
    port = tk.StringVar(win)
    quitting = []

    def on_play():
        # The HTTP server is not running yet; the 'Reload' button on the next
        # window exists to retry once it is.
        webbrowser.open_new(WEB_URL)
        win.destroy()

    def on_quit():
        if mb.askyesno('Verify', 'Really quit?', parent=win):
            quitting.append(True)
            win.destroy()
            return
        mb.showinfo('No', 'Quit has been cancelled', parent=win)

    def on_close():
        if mb.askokcancel('Quit', 'Do you want to quit?', parent=win):
            quitting.append(True)
            win.destroy()

    def on_choose():
        device = choose_serial_port(win)
        if device:
            port.set(device)
            logging.info(f'Selected serial port: {device}')

    win.protocol('WM_DELETE_WINDOW', on_close)
    tk.Label(win, text='Welcome to TotalSync').pack()
    tk.Button(win, text='Quit', command=on_quit).pack()
    tk.Button(win, text='Play', command=on_play).pack()
    tk.Button(win, text='choose COM', command=on_choose).pack()

    win.wait_window()
    return QUIT if quitting else port.get()


class TeensyCommander:
    def __init__(self, serial_port, http_port, ws_port, curses_screen, write_bin=False, use_dummy=False):
        self.n_packet = 0
        self.packets_per_second = 0
        self.packet_timings = []
        # Set up front so shutdown()/__del__ stay safe if __init__ bails out early.
        self.serial = None
        self.serial_reader = None
        self.reader_thread = None
        self.dummy = None
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
                self.shutdown()
                raise SystemExit(1)

        # __enter__() returns the PacketReceiver protocol, not the thread, so keep a
        # reference to the thread as well; shutdown() needs it to stop reading.
        self.reader_thread = serial.threaded.ReaderThread(self.serial, PacketReceiver)
        self.serial_reader = self.reader_thread.__enter__()

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
        """Run the session, returning once the user closes the control window.

        Tk's event loop *is* the main loop: every worker (serial reader, HTTP and
        WebSocket servers, ZMQ subscriber) is a daemon thread, so the main thread
        only has to keep Tk responsive. It used to `time.sleep(1)` in a loop here
        instead, which left the macOS event queue unattended and made the OS
        report the process as "application not responding".
        """
        self.zmq_subscriber.start()
        menu(self)

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

    def shutdown(self):
        """Release the serial port and the ZMQ resources. Safe to call twice."""
        self.alive = False

        if self.reader_thread is not None:
            logging.debug('Stopping serial reader.')
            # Stops the reader thread and closes the port, which also ends the
            # SerialDummy loop (it writes to this very port).
            self.reader_thread.close()
            self.reader_thread = None
            self.serial_reader = None
        elif self.serial is not None and self.serial.is_open:
            self.serial.close()

        if self.zmq_ctx is not None:
            logging.debug('Terminating ZMQ context.')
            # linger=0 drops whatever is still queued. With the default LINGER of
            # -1, term() waits forever for a subscriber that may never connect.
            # term() also makes the subscriber thread's recv_pyobj() raise ETERM,
            # which is how that thread breaks out of its loop.
            self.zmq_ctx.destroy(linger=0)
            self.zmq_ctx = None

    def __del__(self):
        logging.debug('Making sure serial port is closed on exit.')
        if self.serial is not None and self.serial.is_open:
            self.serial.close()


def main(screen, cli_args):
    # SerialDump() opens a tkinter directory chooser, so a root must already exist.
    get_root()
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
    try:
        tc.run_forever()
    except KeyboardInterrupt:
        logging.info('Interrupted, shutting down.')
    finally:
        # Both must happen on the way out: a live ZMQ context or Tk root keeps the
        # process up long enough for macOS to call it unresponsive.
        tc.shutdown()
        destroy_root()


def cli_entry():
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--serial_port', default=None,
                        help='Serial port of the Teensy. If omitted, a startup dialog asks for one.')
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

    # Only ask interactively when no port was given on the command line, so that
    # --help and scripted runs never open a window.
    if cli_args.serial_port is None:
        port = welcome_dialog()
        if port is QUIT:
            logging.info('Quit requested in the startup dialog.')
            destroy_root()
            return
        cli_args.serial_port = port or DEFAULT_SERIAL_PORT

    if cli_args.curses and curses is not None:
        curses.wrapper(main, cli_args)
    else:
        main(None, cli_args)


def menu(commander):
    """Run the control window for a running commander. Returns when it is closed.

    This window stays up for the whole session, so its event loop is what keeps
    the process responsive; see TeensyCommander.run_forever.
    """
    win = tk.Toplevel(get_root())
    win.title('Totalsync')

    def quit_app():
        # Never sys.exit() here, see the comment on QUIT: clearing `alive` and
        # closing the window is what actually ends the session.
        commander.alive = False
        win.destroy()

    def on_quit():
        if mb.askyesno('Verify', 'Really quit?', parent=win):
            quit_app()
            return
        mb.showinfo('No', 'Quit has been cancelled', parent=win)

    def on_reset():
        commander.reset_packet()
        webbrowser.open_new(WEB_URL)

    def on_reload():
        webbrowser.open_new(WEB_URL)

    def on_close():
        if mb.askokcancel('Quit', 'Do you want to quit?', parent=win):
            quit_app()

    # Raw packet count at the previous tick, so the delta can be reported.
    last_count = [0]

    def report_packet_count():
        """Log the received-packet counters, once per watchdog tick."""
        received = commander.serial_dump.n_raw_packets
        new = received - last_count[0]
        last_count[0] = received
        msg = (f'Serial packets: {received} received (+{new}), '
               f'{commander.n_packet} decoded, {commander.packets_per_second:.1f}/s')
        if new:
            logging.info(msg)
        else:
            # A silent port is the failure this counter exists to make visible, so
            # say so at a level that survives the default verbosity.
            logging.warning(msg + ' - nothing received since the last check!')

    def watchdog():
        """Periodic health check, rescheduled on the Tk event loop."""
        if not commander.alive:
            quit_app()
            return
        report_packet_count()
        if commander.shell_gui and commander.shell_gui.alive and not commander.shell_gui.is_alive():
            logging.critical("Shell GUI died!")
            # TODO: attempt to restart the shell GUI
        win.after(1000, watchdog)

    win.protocol('WM_DELETE_WINDOW', on_close)
    tk.Label(win, text='TotalSync is now in use').pack()
    tk.Button(win, text='Quit', command=on_quit).pack()
    tk.Button(win, text='Reset', command=on_reset).pack()
    tk.Button(win, text='Reload', command=on_reload).pack()

    win.after(1000, watchdog)
    win.wait_window()
    commander.alive = False


if __name__ == "__main__":
    cli_entry()
