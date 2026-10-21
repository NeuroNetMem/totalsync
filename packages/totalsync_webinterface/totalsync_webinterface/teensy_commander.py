import argparse
import logging
import os
import signal
import sys
import threading
import time
import webbrowser
from pathlib import Path

import serial
import serial.threaded
import zmq

try:
    import curses
except ImportError:
    curses = None

from .packet import PacketReceiver, pack_command_packet, pack_reset_packet
from .pin_sheet import load_pin_labels
from .serial_dump import SerialDump
from .serial_dummy import SerialDummy
from .web_interface import WebInterface, WS_PORT, HTTP_PORT
from .curses_interface import CursesUI

ZMQ_SERVER_PUB_PORT = 5680
ZMQ_SERVER_SUB_PORT = 5681

# The web interface is served by our own HTTP server, see WebInterface.WEB_DIRECTORY.
WEB_URL = f'http://localhost:{HTTP_PORT}/index.html'


def open_web_interface():
    """Open the interface in a browser.

    The nonce makes the URL one the browser has never seen, so the request has to
    reach the server rather than being answered from cache. Without it, an
    index.html cached before this port started serving redirects would be reused,
    and the redirect that points at the current assets never seen.
    """
    webbrowser.open_new(f'{WEB_URL}?t={time.time_ns():x}')


# Fallback when neither --serial_port nor the control window supplies a port. There is
# no useful cross-platform default: on POSIX the devices are /dev/cu.* or /dev/ttyUSB*
# and differ per machine, so fall back to an empty string and let the SerialException
# SerialPortError from TeensyCommander report it (or use -D to switch to the dummy).
DEFAULT_SERIAL_PORT = 'COM9' if sys.platform == 'win32' else ''


class SerialPortError(Exception):
    """The serial port could not be opened (and -D did not ask for the dummy instead)."""


def prepare_output_directory(directory):
    """Make ``directory`` usable as a recording target, or raise ValueError saying why.

    Called before the serial port and the servers come up, so that a bad path is reported
    while nothing is running yet rather than by the first packet failing to be written.
    """
    directory = Path(directory).expanduser()
    if directory.exists() and not directory.is_dir():
        raise ValueError(f'not a directory: {directory}')
    try:
        directory.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise ValueError(f'cannot create {directory}: {exc}') from None
    if not os.access(directory, os.W_OK):
        raise ValueError(f'not writable: {directory}')
    return directory


class TeensyCommander:
    def __init__(self, serial_port, http_port, ws_port, curses_screen, output_dir,
                 write_bin=False, use_dummy=False, channel_labels=None):
        self.n_packet = 0
        self.packets_per_second = 0
        self.packet_timings = []
        # Set up front so shutdown()/__del__ stay safe if __init__ bails out early.
        self.serial = None
        self.serial_reader = None
        self.reader_thread = None
        self.dummy = None
        self.zmq_ctx = None
        self.alive = True
        self.serial_port = serial_port

        # The serial port first, before anything binds a network port. This is the step
        # the user can get wrong, and the control window lets them pick another port and
        # press Play again - which would fail with "address already in use" if the HTTP,
        # WebSocket and ZMQ servers of the failed attempt were still holding their ports.
        try:
            self.serial = serial.Serial(self.serial_port)
            self.serial.flushInput()
        except serial.SerialException as e:
            logging.error("Can't find serial device: {}".format(e))
            if not use_dummy:
                raise SerialPortError(f"Can't open serial port {self.serial_port!r}: {e}") from None
            logging.warning('Using serial dummy')
            self.dummy = SerialDummy()
            self.serial = self.dummy.ser
            self.serial_port = 'DUMMY'

        try:
            self.zmq_ctx = zmq.Context()
            self.zmq_pub = self.zmq_ctx.socket(zmq.PUB)
            self.zmq_pub.bind(f'tcp://*:{ZMQ_SERVER_PUB_PORT}')

            self.zmq_sub = self.zmq_ctx.socket(zmq.SUB)
            self.zmq_sub.setsockopt_string(zmq.SUBSCRIBE, "")
            self.zmq_sub.bind(f'tcp://*:{ZMQ_SERVER_SUB_PORT}')

            self.shell_gui = CursesUI(self, curses_screen) if curses_screen is not None else None
            time.sleep(0.05)  # give some time to let log display catch all startup messages

            self.output_dir = Path(output_dir)
            self.serial_dump = SerialDump(self.output_dir)
            self.web_server = WebInterface(http_port, ws_port, self, channel_labels=channel_labels)
        except BaseException:
            # Release the serial port and the ZMQ context, which are already open.
            self.shutdown()
            raise

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

    def start(self):
        """Start relaying commands from ZMQ to the Teensy. Returns immediately.

        Every worker (serial reader, HTTP and WebSocket servers, ZMQ subscriber) is a
        daemon thread; the main thread belongs to the control window's Qt event loop,
        which is what keeps the process responsive (see run_gui).
        """
        self.zmq_subscriber.start()

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


def run_gui(screen, cli_args, channel_labels=None):
    """Show the control window and run the session from it. Returns once it is closed.

    ``screen`` is the curses screen with -C and None otherwise; the commander that Play
    creates attaches its text-mode status interface to it.
    """
    # Imported here rather than at the top: control_window imports this module for
    # TeensyCommander, so importing it back at module level would be circular.
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import QApplication

    from .control_window import ControlWindow

    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setApplicationName('TotalSync')
    window = ControlWindow(cli_args, channel_labels, curses_screen=screen)

    # Ctrl-C in the terminal. Qt's event loop runs in C++, where Python never gets to
    # look at a pending signal, so a KeyboardInterrupt would only be raised - if at all -
    # once some unrelated Python callback happened to run. Handle SIGINT explicitly
    # instead, and give the interpreter a regular slot in which to notice it.
    signal.signal(signal.SIGINT, lambda *_: window.force_quit())
    wakeup = QTimer()
    wakeup.timeout.connect(lambda: None)
    wakeup.start(200)

    window.show()
    window.raise_()
    # Given the port and the directory on the command line there is nothing left to ask,
    # so start straight away, as the old startup dialog was skipped. Queued rather than
    # called directly, so that a warning from play() has a window on screen to sit on.
    if cli_args.serial_port is not None and cli_args.output_dir is not None:
        QTimer.singleShot(0, window.play)

    try:
        app.exec()
    finally:
        wakeup.stop()
        signal.signal(signal.SIGINT, signal.default_int_handler)
        if window.commander is not None:
            # A live ZMQ context keeps the process up long enough for macOS to call it
            # unresponsive, so release it on every way out.
            window.commander.shutdown()


def cli_entry():
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--serial_port', default=None,
                        help='Serial port of the Teensy, filled into the control window. Together '
                             'with -o the session starts without waiting for Play.')
    parser.add_argument('-o', '--output-dir', default=None, metavar='DIR',
                        help='Directory to write the recording into, filled into the control '
                             'window (default: the working directory).')
    parser.add_argument('--no-browser', action='store_true',
                        help='Do not open the web interface in a browser at startup')
    parser.add_argument('-w', '--ws_port', type=int, default=WS_PORT,
                        help=f'Websocket port (default {WS_PORT}). Note that the browser '
                             f'client hardcodes {WS_PORT}, so changing this stops the '
                             f'page from receiving data.')
    parser.add_argument('-H', '--http_port', type=int, default=HTTP_PORT,
                        help=f'Port the web interface is served on (default {HTTP_PORT})')
    parser.add_argument('-B', '--binfile', action='store_true', help='Write decoded binary serial dump file')
    parser.add_argument('-C', '--curses', action='store_true', help='Use cursesUI in terminal')
    parser.add_argument('-D', '--dummy', action='store_true',
                        help='Use serial dummy if no valid serial device available')
    parser.add_argument('-v', '--verbose', action='count', default=2, help="Increase logging verbosity")
    parser.add_argument('--pinsheet', default=None, metavar='PATH',
                        help='pinSheet.json describing what each pin is wired to. Channels with '
                             'a "for" entry are labelled with it in the web interface; the rest '
                             'keep their default names.')

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

    # Read the pin sheet before anything opens: a bad path should fail here, not
    # after the serial port and the servers are already up.
    channel_labels = load_pin_labels(cli_args.pinsheet) if cli_args.pinsheet else {}

    if cli_args.curses and curses is not None:
        curses.wrapper(run_gui, cli_args, channel_labels)
    else:
        run_gui(None, cli_args, channel_labels)


if __name__ == "__main__":
    cli_entry()
