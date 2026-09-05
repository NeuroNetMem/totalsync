import functools
import http.server
import json
import json
import logging
import re
import socketserver
import threading
from pathlib import Path

import numpy as np

from webinterface.totalsync.websocket_server import WebsocketServer as ReconnectingWebsocketServer

WS_PORT = 5678
HTTP_PORT = 8000
WEB_DIRECTORY = (Path(__file__).parent / '../web').resolve().as_posix()

# The pin sheet labels are baked into index.html as window.CHANNEL_LABELS. This route
# serves the same mapping as JSON, for checking what the server thinks the labels are
# ("curl http://localhost:8000/channel_labels.json"); the page does not depend on it.
LABELS_ROUTE = '/channel_labels.json'
INDEX_ROUTES = ('/', '/index.html')

# Everything in web/ is served under a /v<token>/ prefix, and a request for the index
# without one is redirected to the current token.
#
# A browser told nothing about how long a file stays fresh guesses, and it guesses from
# the file's date: index.html is dated 2024, which buys months of reuse without ever
# asking the server again. So an interface.js edited today keeps being served from
# cache, and that is what makes changes to the interface appear in one tab and not the
# next -- the Reload and Reset buttons open a new tab, which reads the cache, rather
# than reloading, which revalidates. Under the prefix, the relative URLs in index.html
# *and* the module imports inside interface.js all resolve to a URL the cache has never
# seen as soon as any one file changes, so there is nothing for it to reuse.
VERSION_PREFIX = re.compile(r'^/v[0-9a-f]+(?=/)')


def asset_version():
    """Cache-busting token for web/: the newest modification time in it.

    Not a random value per run: as long as nothing changes, the URLs stay the same and
    the browser can go on caching. It is only an edit that invalidates them.
    """
    try:
        newest = max(p.stat().st_mtime_ns
                     for p in Path(WEB_DIRECTORY).rglob('*') if p.is_file())
    except (OSError, ValueError):
        # A web directory that cannot be read, or holds nothing, is a problem that the
        # request for index.html will report; here just fall back to busting nothing.
        newest = 0
    return f'v{newest:x}'


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
    # Set per request by parse_request(); a default keeps a request that never got
    # that far from tripping over the attribute.
    versioned = False

    def __init__(self, *args, channel_labels=None, **kwargs):
        # BaseRequestHandler.__init__() serves the request there and then, so
        # everything do_GET needs has to be in place before delegating to it.
        self.channel_labels = channel_labels or {}
        super().__init__(*args, directory=WEB_DIRECTORY, **kwargs)

    def parse_request(self):
        """Take the cache-busting prefix off the path, so nothing below sees it.

        Done here rather than in do_GET because it is the one place every method
        passes through, and translate_path resolves self.path against web/ where a
        /v<token>/ prefix would just look like a directory that does not exist.
        """
        if not super().parse_request():
            return False
        self.versioned = VERSION_PREFIX.match(self.path) is not None
        self.path = VERSION_PREFIX.sub('', self.path, count=1)
        return True

    def do_GET(self):
        if not self.serve_route():
            super().do_GET()

    def do_HEAD(self):
        # Routed like GET, so that a HEAD cannot describe a different resource than
        # the one a GET returns -- which is exactly what a cache would go on to store.
        if not self.serve_route():
            super().do_HEAD()

    def serve_route(self):
        """Serve the index with labels baked in, or the labels route from memory.

        False when the path is neither, meaning it is a file to be read from disk.
        """
        path = self.path.partition('?')[0]
        if path in INDEX_ROUTES:
            if self.versioned:
                self.send_index()
            else:
                # A bookmark, or the URL totalsync opens. Send the browser to the
                # prefixed index, so that page's assets are fetched from under the
                # prefix too.
                self.redirect_to_current_version()
        elif path == LABELS_ROUTE:
            self.send_body(json.dumps(self.channel_labels).encode(), 'application/json')
        else:
            return False
        return True

    def redirect_to_current_version(self):
        self.send_response(302)
        self.send_header('Location', f'/{asset_version()}/index.html')
        self.send_header('Content-Length', '0')
        self.end_headers()

    def send_index(self):
        """Serve index.html with the channel labels baked into the document.

        The labels belong in the page that builds the interface. Fetching them
        separately means the interface can be built before, or entirely without,
        them, and a request that quietly fails leaves every trace showing its
        default name with nothing to say why.
        """
        index = Path(WEB_DIRECTORY) / 'index.html'
        try:
            html = index.read_text()
        except OSError as e:
            logging.error(f'Cannot read {index}: {e}')
            self.send_error(404, 'index.html not found')
            return

        # A literal "</script>" in the JSON would close the block early; escaping the
        # three characters that can start markup keeps whatever a pin sheet contains
        # inert. json.dumps has already dealt with quotes and backslashes.
        payload = (json.dumps(self.channel_labels)
                   .replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026'))
        # A classic script in the head runs before the deferred module that reads it.
        tag = f'<script>window.CHANNEL_LABELS = {payload};</script>\n'
        if '</head>' in html:
            html = html.replace('</head>', tag + '</head>', 1)
        else:
            logging.warning(f'{index} has no </head>: serving it without channel labels.')

        self.send_body(html.encode(), 'text/html; charset=utf-8')

    def send_body(self, body, content_type):
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(body)

    def end_headers(self):
        # Says what SimpleHTTPRequestHandler leaves the browser to guess (see
        # VERSION_PREFIX). Belt and braces next to the versioned URLs, and what keeps
        # a tab left open for a week honest. It still allows a cheap 304; it only
        # forbids reusing a copy without checking first.
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()


class HTTPServer(threading.Thread):
    def __init__(self, http_port, channel_labels=None):
        super(HTTPServer, self).__init__(daemon=True)

        logging.info(f"Launching HTTP server for directory {WEB_DIRECTORY} on port {http_port}")

        # allow reuse of previously bound endpoints
        socketserver.TCPServer.allow_reuse_address = True
        # The handler class is instantiated per request, so bind the labels to it
        # rather than reaching for a module global.
        handler = functools.partial(HttpRequestHandler, channel_labels=channel_labels)
        self.server = socketserver.TCPServer(("", http_port), handler)

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
    def __init__(self, http_port, ws_port, teensy_commander, channel_labels=None):
        self.http_port = http_port
        self.ws_port = ws_port
        self.http_server = HTTPServer(http_port=self.http_port, channel_labels=channel_labels)
        self.tc = teensy_commander
        self.ws_server = WSServer(ws_port=self.ws_port)
        self.ws_server.msg_callback = self.tc.send

        self.http_server.start()
        self.ws_server.start()

    def handle_packet(self, packet):
        self.ws_server.handle_packet(packet)
