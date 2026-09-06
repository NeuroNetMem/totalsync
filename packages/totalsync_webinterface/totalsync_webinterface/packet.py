import base64
import logging
import struct
import traceback
from collections import namedtuple

from cobs import cobs
from serial.threaded import Packetizer

# uint8_t type;          // 1 B, packet type
# uint8_t size;          // 1 B, packet size
# uint16_t crc16;        // 2 B, CRC16
# unsigned long packetID;// 4 B, running packet count

# unsigned long ts_start;// 4 B, gather start timestamp
# unsigned long ts_end;  // 4 B, transmit timestamp
# uint16_t analog[8];    // 16 B, ADC values
# long states[8];        32 16 B, state variables (encoder, speed, etc)

# uint16_t digitalIn;    // 2 B, digital inputs
# uint16_t digitalOut;    // 2 B, digital outputs
# uint8_t padding[1];    // 1 B, align to 4B

DataPacketDesc = {'type': 'B',
                  'size': 'B',
                  'crc16': 'H',
                  'packetID': 'I',
                  'us_start': 'I',
                  'us_end': 'I',
                  'analog': '8H',
                  'states': '8l',
                  'digitalIn': '2H',
                  'digitalOut': '3B',
                  'padding': 'x'}

DataPacket = namedtuple('DataPacket', DataPacketDesc.keys())
DataPacketStruct = '<' + ''.join(DataPacketDesc.values())
DataPacketSize = struct.calcsize(DataPacketStruct)

Instructions = {'low': 0,
                'high': 1,
                'toggle': 2,
                'pulse': 3,
                'state': 4,
                'unity': 5,
                'handshake': 149,
                'reset': 6}
InstructionsStructs = {
    'low': 'B',
    'high': 'B',
    'toggle': 'B',
    'pulse': 'L',
    'state': 'l',
    'unity': 'B',
    'reset': 'B'
}

CommandPacketHeaderDesc = {'type': 'B',
                           'size': 'B',
                           'crc16': 'H',
                           'instruction': 'B'}
CommandPacketHeader = namedtuple('CommandPacket', CommandPacketHeaderDesc.keys())
CommandPacketHeaderStruct = '<' + ''.join(CommandPacketHeaderDesc.values())
CommandPacketHeaderSize = struct.calcsize(CommandPacketHeaderStruct)

PinPulsePacket = {'pin': 'B',
                  'duration': 'H'}
PinPulsePacketStruct = '<BHx'


def pack_data_packet(packet_obj):
    raise NotImplemented('data packing not ready.')


def pack_command_packet(packet_obj):
    logging.info(f'Packing CommandPacket {packet_obj}')
    data = packet_obj['data']
    instruction = packet_obj['instruction']
    data_arr = b''
    for ds in data:
        data_arr += struct.pack('<B' + InstructionsStructs[instruction], *ds)
    cmd_p = struct.pack(CommandPacketHeaderStruct,
                        1, CommandPacketHeaderSize + len(data_arr), 0, Instructions[instruction])
    arr = cmd_p + data_arr
    logging.info(cobs.encode(arr) + b'\0');
    return cobs.encode(arr) + b'\0'


def pack_reset_packet():
    logging.info(f'Packing CommandPacket ')
    data = [(0,1)]
    instruction = 'reset'
    data_arr = b''
    for ds in data:
        data_arr += struct.pack('<B' + InstructionsStructs[instruction], *ds)
    cmd_p = struct.pack(CommandPacketHeaderStruct,
                        1, CommandPacketHeaderSize + len(data_arr), 0, Instructions[instruction])
    arr = cmd_p + data_arr
    logging.info(cobs.encode(arr) + b'\0');
    return cobs.encode(arr) + b'\0'


class PacketReceiver(Packetizer):
    raw_callbacks = []
    decoded_callbacks = []
    packet_callbacks = []

    def connection_made(self, transport):
        super(PacketReceiver, self).connection_made(transport)

    def handle_packet(self, encoded):
        """Handle an incoming packet from the serial port. The Packetizer has stripped the
        line-termination \0 byte from it already.
        """
        try:
            assert (len(encoded))
            for cb in self.raw_callbacks:
                cb(encoded)
        except BaseException as e:
            logging.critical(e)

        # COBS decode the array
        try:
            decoded = cobs.decode(encoded)
        except cobs.DecodeError as e:
            logging.warning(str(e))
            return

        try:
            for cb in self.decoded_callbacks:
                cb(encoded)
        except BaseException as e:
            logging.critical(e)

        # Unpack given the type of data
        packet_type = decoded[0]
        if packet_type == 0:
            self.unpack_data_packet(decoded)

        elif packet_type == 1:
            self.unpack_command_packet(decoded)

        elif packet_type == 2:
            logging.error(f'Received error packet {decoded}')

        else:
            logging.error(f'Received unknown packet type: {packet_type} in packet {decoded}')

    def unpack_data_packet(self, arr):
        """Handle a data packet by extracting its fields.
        """
        if len(arr) != DataPacketSize:
            logging.warning(f"Incorrect data size. Is: {len(arr)}, expected: {DataPacketSize}. Packet: {arr}")
            return

        # stupid manual struct unpacking is stupid
        s = struct.unpack(DataPacketStruct, arr)
        dp = DataPacket(type=s[0], size=s[1], crc16=s[2], packetID=s[3], us_start=s[4], us_end=s[5],
                        analog=s[6:14], states=s[14:22], digitalIn=s[22], digitalOut=s[23], padding=None)

        # hand over packets to interested parties...
        for fn_packet_callback in self.packet_callbacks:
            try:
                fn_packet_callback(dp)
            # TODO: EVIL! DON'T! NO! NO! NO!
            except BaseException as e:
                logging.critical(e)
                raise

    def unpack_command_packet(self, arr):
        """Handle a command packet by extracting its fields.
        """
        raise NotImplementedError
        # if len(arr) != CommandPacketHeaderSize:
        #     logging.warning(f"Incorrect data size. Is: {len(arr)}, expected: {CommandPacketHeaderSize}. Packet: {arr}")
        #     return
        #
        # # stupid manual struct unpacking is stupid
        # s = struct.unpack(CommandPacketHeaderStruct, arr)
        # dp = CommandPacketHeader(type=s[0], size=s[1], crc16=s[2], instruction=s[3], target=s[4], message=s[5:18],
        #                    padding=None)
        #
        # # hand over packets to interested parties...
        # for fn_packet_callback in self.packet_callbacks:
        #     try:
        #         fn_packet_callback(dp)
        #     except BaseException as e:
        #         logging.critical(e)

    def connection_lost(self, exc):
        if exc:
            print('Serial connection loss: ', exc)
            logging.debug(f'Serial connection loss: {exc}')
            traceback.print_exc()
