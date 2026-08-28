import logging
import math
import struct
import threading
import time

import serial
from cobs import cobs

from webinterface.totalsync.Packet import DataPacketStruct


# uint8_t type;          // 1 B, packet type
# uint8_t size;          // 1 B, packet size
# uint16_t crc16;        // 2 B, CRC16
# unsigned long packetID;// 4 B, running packet count

# unsigned long ts_start;// 4 B, gather start timestamp
# unsigned long ts_end;  // 4 B, transmit timestamp
# uint16_t analog[8];    // 16 B, ADC values
# int16_t states[8];     // 16 B, state variables (encoder, speed, etc)

# uint16_t digitalIn;    // 2 B, digital inputs
# uint8_t digitalOut;    // 1 B, digital outputs
# uint8_t padding[0];    // 1 B, align to 4B

class SerialDummy(threading.Thread):
    def __init__(self):
        super(SerialDummy, self).__init__()
        self.ser = serial.serial_for_url('loop://')
        self.daemon = True
        self.n_packet = 0
        self.interval = 1

        self.start()

    def run(self):
        logging.info('SerialDummy starting')
        while self.is_alive() and self.ser.is_open:
            self.emit()
            time.sleep(self.interval / 1000)
        self.ser.close()

    def emit(self):
        for n in range(10):
            rp = self.random_packet()
            if rp:
                self.ser.write(rp)
                self.n_packet += 1

    def random_packet(self):
        packet_type = 0
        packet_size = struct.calcsize(DataPacketStruct)
        packet_crc = 777
        packet_id = self.n_packet

        packet_us_start = self.n_packet * self.interval * 1000 % (2 ** 32 - 1)
        packet_us_end = self.n_packet * self.interval * 1000 % (2 ** 32 - 1)
        packet_analog = self.analog()
        packet_states = self.states()

        packet_digitalIn = int(self.n_packet / 10 % 2 ** 16)
        packet_digitalOut = int(self.n_packet / 10 % 2 ** 8)

        packet = []
        packet.extend([packet_type, packet_size, packet_crc, packet_id,
                       packet_us_start, packet_us_end, *packet_analog, *packet_states,
                       packet_digitalIn, packet_digitalOut])

        try:
            ps = struct.pack(DataPacketStruct, *packet)
        except struct.error as e:
            logging.error(e)
            print(packet)
            return

        enc = cobs.encode(ps)
        return enc + b'\0'

    def analog(self):
        arr = []
        for n in range(8):
            val = int((math.sin((self.n_packet + n) / (2 ** (n + 1))) + 1) * (2 ** (n + 8) - 1))
            arr.append(val)
        return arr

    def states(self):
        arr = []
        for n in range(8):
            val = int((math.sin((self.n_packet + n + 500) / (2 ** (n + 1)))) * (2 ** (n + 8) - 1))
            arr.append(val)
        return arr
