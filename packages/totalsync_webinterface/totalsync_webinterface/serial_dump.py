import base64
from pathlib import Path
from datetime import datetime
import logging


class SerialDump:
    """Writes the packets coming off the serial line into a timestamped session file.

    ``output_dir`` is asked for by the caller, not by this class.  It used to open a
    tkinter directory chooser from here, which meant the recorder could not be used
    without a GUI and -- worse -- that the dialog appeared at whatever point in startup
    this object happened to be constructed, which was after the browser had already been
    pointed at a server that was not listening yet.  Choosing the directory is a job for
    whoever is talking to the user; see ``choose_output_directory`` in teensy_commander.
    """

    def __init__(self, output_dir):
        self.output_dir = Path(output_dir)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S_%f")[:-3]
        self.f_b64 = self.output_dir / f'{stamp}.b64'
        self.f_bin = self.f_b64.with_suffix('.bin')

        self.cobs_file = None
        self.bin_file = None

        # Counts every framed packet that came off the serial line, including those
        # that later fail to COBS-decode or unpack: handle_raw runs before any of
        # that happens (see Packet.PacketReceiver.handle_packet). Comparing it with
        # TeensyCommander.n_packet, which only counts fully unpacked data packets,
        # is what makes a flaky connection visible.
        self.n_raw_packets = 0

    def handle_raw(self, arr):
        """Write the cobs-encoded, 0-terminated data as base64 encoded string to log file,
        one line per packet.

        To allow re-parsing of the array with a cobs-decoder, we re-add the 0 terminator that was
        removed by the serial packetizer. (This is completely unnecessary and here for backwards
        compatibility).
        """
        # Count the arrival, not the successful write, so a full disk or a closed
        # file does not hide the fact that the packet did come in. Only the serial
        # ReaderThread ever increments this, so a plain += needs no lock.
        self.n_raw_packets += 1

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
        # Either file is still None if no packet of that kind ever arrived, which is
        # the normal case for .bin unless -B was given.
        for handle in (self.cobs_file, self.bin_file):
            if handle is not None:
                handle.close()
