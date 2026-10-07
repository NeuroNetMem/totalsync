"""The control window of the `totalsync` command.

One window for the whole session: pick the serial port and the output directory, press
Play, and from then on Reset, Reload and Quit, with the packet counters in the status bar
and the address of the browser interface ready to be copied. The data itself is only
ever shown in the browser.
"""

import logging
import os
import time
from pathlib import Path

import serial.tools.list_ports
import zmq
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (QFileDialog, QFormLayout, QGroupBox, QHBoxLayout, QLabel,
                               QLineEdit, QComboBox, QMainWindow, QMessageBox, QPushButton,
                               QSizePolicy, QVBoxLayout, QWidget)

from .teensy_commander import (DEFAULT_SERIAL_PORT, WEB_URL, SerialPortError, TeensyCommander,
                               open_web_interface, prepare_output_directory)

# How often the status bar is refreshed, and the window through which "nothing received"
# is judged: a port that stays silent for one whole tick is reported.
STATUS_INTERVAL_MS = 1000


def default_output_directory():
    """Where a session is written unless the user says otherwise.

    The working directory is where the user cd'd to before running the command, so it is
    the best guess available; a Finder or desktop launch, where the working directory is
    '/', is not - nobody wants to record into '/' and it is not writable anyway - hence the
    fall back to $HOME.
    """
    start = Path.cwd()
    if start == Path(start.anchor) or not os.access(start, os.W_OK):
        start = Path.home()
    return start


class ControlWindow(QMainWindow):
    """The session window. Idle until Play, then running until it is closed.

    ``commander`` is None until Play succeeds; whoever runs the event loop has to shut
    it down once the window is gone (see teensy_commander.run_gui).
    """

    def __init__(self, cli_args, channel_labels=None, curses_screen=None):
        super().__init__()
        self.cli_args = cli_args
        self.channel_labels = channel_labels
        self.curses_screen = curses_screen
        self.commander = None
        # Raw and decoded packet counts, and the time, at the previous status tick. The
        # rate shown is worked out from these rather than taken from the commander, whose
        # packets_per_second is only recalculated when a packet arrives and so stays at
        # its last value, however high, for as long as the port is silent.
        self.last_count = 0
        self.last_decoded = 0
        self.last_tick = 0.0
        # When the port went quiet, or None while packets are arriving. Silence is a
        # state: it is logged once when it starts and once when it ends, not every tick.
        self.silent_since = None
        # Set once the user has confirmed (or a confirmation is not wanted), so that
        # closeEvent does not ask a second time.
        self.quit_confirmed = False

        self.setWindowTitle('TotalSync')
        self.setMinimumWidth(560)

        # --- Session: where the data comes from and where it goes.
        self.port_combo = QComboBox()
        # Editable, so that a port the enumeration misses can still be typed in.
        self.port_combo.setEditable(True)
        self.port_combo.setMinimumContentsLength(24)
        refresh_button = QPushButton('Refresh')
        refresh_button.setToolTip('Look for serial ports again')
        refresh_button.clicked.connect(self.refresh_ports)
        port_row = QHBoxLayout()
        port_row.addWidget(self.port_combo, 1)
        port_row.addWidget(refresh_button)

        self.dir_edit = QLineEdit()
        self.dir_edit.textChanged.connect(self.update_buttons)
        browse_button = QPushButton('Browse…')
        browse_button.clicked.connect(self.browse_output_directory)
        dir_row = QHBoxLayout()
        dir_row.addWidget(self.dir_edit, 1)
        dir_row.addWidget(browse_button)

        self.session_box = QGroupBox('Session')
        form = QFormLayout(self.session_box)
        form.addRow('Serial port:', port_row)
        form.addRow('Output directory:', dir_row)

        # --- Data logger: the address of the browser interface.
        url_edit = QLineEdit(WEB_URL)
        url_edit.setReadOnly(True)
        copy_button = QPushButton('Copy')
        copy_button.setToolTip('Copy the address to the clipboard')
        copy_button.clicked.connect(self.copy_url)
        url_box = QGroupBox('Data logger (open in a browser)')
        url_row = QHBoxLayout(url_box)
        url_row.addWidget(url_edit, 1)
        url_row.addWidget(copy_button)

        # --- Actions. Same behaviour as the buttons of the old dialogs.
        self.play_button = QPushButton('Play')
        self.play_button.setDefault(True)
        self.play_button.clicked.connect(self.play)
        self.reset_button = QPushButton('Reset')
        self.reset_button.setToolTip('Reset the Teensy and reopen the browser interface')
        self.reset_button.clicked.connect(self.reset)
        self.reload_button = QPushButton('Reload')
        self.reload_button.setToolTip('Reopen the browser interface')
        self.reload_button.clicked.connect(open_web_interface)
        quit_button = QPushButton('Quit')
        quit_button.clicked.connect(self.close)
        buttons = QHBoxLayout()
        for button in (self.play_button, self.reset_button, self.reload_button):
            buttons.addWidget(button)
        buttons.addStretch(1)
        buttons.addWidget(quit_button)

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.addWidget(self.session_box)
        layout.addWidget(url_box)
        layout.addLayout(buttons)
        self.setCentralWidget(central)

        # A normal (not permanent) widget, so that a temporary showMessage() such as
        # "URL copied" covers it for a moment and then gives it back.
        self.status_label = QLabel('Not running')
        # Two lines of whatever width the window has: Ignored, so that a long directory
        # cannot set the minimum width of the whole window, which it otherwise would.
        self.status_label.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.statusBar().addWidget(self.status_label, 1)

        self.status_timer = QTimer(self)
        self.status_timer.setInterval(STATUS_INTERVAL_MS)
        self.status_timer.timeout.connect(self.update_status)

        self.refresh_ports()
        if cli_args.serial_port:
            self.port_combo.setEditText(cli_args.serial_port)
        self.dir_edit.setText(str(cli_args.output_dir or default_output_directory()))
        self.update_buttons()

    # --- setup state

    def refresh_ports(self):
        """Fill the port list from the ports present now, keeping what was typed."""
        current = self.port_combo.currentText()
        ports = sorted(comport.device for comport in serial.tools.list_ports.comports())
        logging.info('Known serial ports: ' + repr(ports))
        self.port_combo.clear()
        self.port_combo.addItems(ports)
        self.port_combo.lineEdit().setPlaceholderText(
            'Serial port' if ports else 'No serial ports found')
        if current or not ports:
            self.port_combo.setEditText(current)

    def browse_output_directory(self):
        start = self.dir_edit.text() or str(default_output_directory())
        directory = QFileDialog.getExistingDirectory(
            self, 'Where should TotalSync write this session?', start)
        if directory:
            self.dir_edit.setText(directory)

    def update_buttons(self):
        running = self.commander is not None
        self.session_box.setEnabled(not running)
        self.play_button.setEnabled(not running and bool(self.dir_edit.text().strip()))
        self.reset_button.setEnabled(running)
        self.reload_button.setEnabled(running)

    def play(self):
        """Start the session. On any failure say why and stay in the setup state."""
        if self.commander is not None:
            return
        try:
            output_dir = prepare_output_directory(self.dir_edit.text().strip())
        except ValueError as exc:
            QMessageBox.warning(self, 'Output directory', f'Cannot record into that directory: {exc}')
            return

        port = self.port_combo.currentText().strip() or DEFAULT_SERIAL_PORT
        args = self.cli_args
        logging.info(f'Launching Teensy Commander on serial port {port} and the web interface '
                     f'on ports HTTP:{args.http_port} and WS:{args.ws_port}')
        try:
            commander = TeensyCommander(serial_port=port,
                                        http_port=args.http_port,
                                        ws_port=args.ws_port,
                                        curses_screen=self.curses_screen,
                                        output_dir=output_dir,
                                        write_bin=args.binfile,
                                        use_dummy=args.dummy,
                                        channel_labels=self.channel_labels)
        except SerialPortError as exc:
            QMessageBox.warning(self, 'Serial port', f'{exc}\n\nPick another port and press Play again.')
            return
        except (OSError, zmq.ZMQError) as exc:
            # Typically another totalsync still holding the HTTP, WebSocket or ZMQ ports.
            QMessageBox.critical(self, 'Cannot start', f'Could not start the servers: {exc}')
            return

        commander.start()
        self.commander = commander
        logging.info(f'Recording into {output_dir}')
        # Only now: reaching this line means the servers are listening, so the page will
        # load rather than greet the user with "unable to connect".
        if not args.no_browser:
            open_web_interface()

        self.last_count = 0
        self.last_decoded = 0
        self.last_tick = time.monotonic()
        self.silent_since = None
        self.update_buttons()
        self.update_status()
        self.status_timer.start()

    # --- running state

    def reset(self):
        self.commander.reset_packet()
        open_web_interface()

    def copy_url(self):
        QGuiApplication.clipboard().setText(WEB_URL)
        self.statusBar().showMessage(f'Copied {WEB_URL}', 2000)

    def update_status(self):
        """Show the packet counters; also the session's periodic health check."""
        commander = self.commander
        if not commander.alive:
            self.force_quit()
            return

        now = time.monotonic()
        received = commander.serial_dump.n_raw_packets
        decoded = commander.n_packet
        new = received - self.last_count
        elapsed = now - self.last_tick
        rate = (decoded - self.last_decoded) / elapsed if elapsed > 0 else 0.0
        self.last_count, self.last_decoded, self.last_tick = received, decoded, now

        counters = f'{received} received (+{new})  ·  {decoded} decoded  ·  {rate:.1f}/s'
        if new:
            if self.silent_since is not None:
                logging.info(f'Serial packets arriving again after '
                             f'{now - self.silent_since:.0f} s')
                self.silent_since = None
                self.status_label.setStyleSheet('')
            logging.debug(f'Serial packets: {counters}')
        else:
            if self.silent_since is None:
                # A silent port is the failure this counter exists to make visible, so it
                # goes to the log at a level that survives the default verbosity - once.
                # Silent since the previous tick, the last time anything was seen.
                self.silent_since = now - elapsed
                self.status_label.setStyleSheet('color: #c62828; font-weight: bold;')
                logging.warning(f'Serial packets: {received} received, {decoded} decoded'
                                ' - nothing received since the last check!')
            counters = (f'nothing received for {now - self.silent_since:.0f} s  ·  '
                        f'{received} received  ·  {decoded} decoded  ·  {rate:.1f}/s')

        # Shortened in the middle to what fits, so both ends of the path stay readable;
        # the tooltip has it in full.
        directory = str(commander.output_dir)
        self.status_label.setToolTip(directory)
        prefix = 'Recording to '
        metrics = self.status_label.fontMetrics()
        room = max(self.status_label.width() - metrics.horizontalAdvance(prefix), 100)
        directory = metrics.elidedText(directory, Qt.TextElideMode.ElideMiddle, room)
        self.status_label.setText(f'{commander.serial_port}  ·  {counters}\n{prefix}{directory}')

        shell_gui = commander.shell_gui
        if shell_gui and shell_gui.alive and not shell_gui.is_alive():
            logging.critical("Shell GUI died!")
            # TODO: attempt to restart the shell GUI

    # --- quitting

    def force_quit(self):
        """Close without asking: Ctrl-C in the terminal, or the session ended by itself."""
        self.quit_confirmed = True
        self.close()

    def closeEvent(self, event):
        if not self.quit_confirmed:
            answer = QMessageBox.question(self, 'Quit', 'Really quit TotalSync?')
            if answer != QMessageBox.StandardButton.Yes:
                event.ignore()
                return
            self.quit_confirmed = True
        self.status_timer.stop()
        event.accept()
