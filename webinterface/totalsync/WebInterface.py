import http.server
import json
import json
import logging
import socketserver
import threading
from pathlib import Path

import numpy as np

from webinterface.totalsync.websocket_server import WebsocketServer as ReconnectingWebsocketServer

WS_PORT = 5678
HTTP_PORT = 8000
WEB_DIRECTORY = (Path(__file__).parent / '../web').resolve().as_posix()


def num_to_bits(num, nbits=16, reverse=True):
    """Integer to list of bits"""
    # TODO: pad zeros with string formatter
    bl = list(map(int, bin(num)[2:]))
    bl = list(map(int, bin(num)[2:]))
    # pad with leading zeros
    bl = [0] * (nbits - len(bl)) + bl
    return list(reversed(bl)) if reverse else bl


class NumpyEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, np.ndarray):
            return obj.tolist()
        return json.JSONEncoder.default(self, obj)


class HttpRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIRECTORY, **kwargs)


class HTTPServer(threading.Thread):
    def __init__(self, http_port):
        super(HTTPServer, self).__init__(daemon=True)

        logging.info(f"Launching HTTP server for directory {WEB_DIRECTORY} on port {http_port}")

        # allow reuse of previously bound endpoints
        socketserver.TCPServer.allow_reuse_address = True
        self.server = socketserver.TCPServer(("", http_port), HttpRequestHandler)

    def run(self):
        self.server.serve_forever()


class WSServer(threading.Thread):
    def __init__(self, ws_port):
        super(WSServer, self).__init__(daemon=True)
        logging.info(f"Starting Websocket Server on port {ws_port}")
        self.server = ReconnectingWebsocketServer(host="0.0.0.0", port=ws_port)
        self.server.set_fn_new_client(self.ws_client_connect)
        self.server.set_fn_client_left(self.ws_client_left)
        self.server.set_fn_message_received(self.ws_msg_rcv)
        self.msg_callback = None

    def run(self):
        self.server.run_forever()

    @staticmethod
    def ws_client_connect(client, server):
        client_id = "Unknown" if client is None else client["id"]
        logging.debug(f'Client {client_id} connected to WebSocket server.')

    @staticmethod
    def ws_client_left(client, server):
        client_id = "Unknown" if client is None else client["id"]
        logging.debug(f'Client {client_id} disconnected from WebSocket server.')

    def ws_msg_rcv(self, client, server, message):
        # TODO: Match pinout, which pins are input, which output
        logging.debug(f"WS_msg {message} from {client}")
        if self.msg_callback is None:
            return

        try:
            if message.startswith('digital'):
                pin_type, pin_direction, instruction, pin = message.split('_')
                instr = {'instruction': instruction, 'data': [(int(pin), 1)]}
                logging.debug(instr)
                self.msg_callback(instr)
            else:
                logging.debug('Unknown WS Message:' + message)
        except BaseException as e:
            logging.error(e)

    def handle_packet(self, packet):
        if packet.type == 0:
            js = json.dumps({'us_start': packet.us_start, 'us_end': packet.us_end,
                             'analog': packet.analog, 'states': packet.states,
                             'digitalIn': num_to_bits(packet.digitalIn, nbits=16),
                             'digitalOut': num_to_bits(packet.digitalOut, nbits=16)},
                            cls=NumpyEncoder)
            self.server.send_message_to_all(js)


class WebInterface:
    def __init__(self, http_port, ws_port, teensy_commander):
        self.http_port = http_port
        self.ws_port = ws_port
        self.http_server = HTTPServer(http_port=self.http_port)
        self.tc = teensy_commander
        self.ws_server = WSServer(ws_port=self.ws_port)
        self.ws_server.msg_callback = self.tc.send

        self.http_server.start()
        self.ws_server.start()

    def handle_packet(self, packet):
        self.ws_server.handle_packet(packet)
