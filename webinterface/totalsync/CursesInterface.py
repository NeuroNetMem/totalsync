import curses
import logging
import sys
import threading
import time
from datetime import datetime



class CursesHandler(logging.Handler):
    def __init__(self, window):
        logging.Handler.__init__(self)
        self.window = window

    def emit(self, record):
        try:
            msg = self.format(record)
            lvl = record.levelno
            color = 0
            if lvl > 10:
                color = 3
            if lvl > 20:
                color = 13
            if lvl > 30:
                color = 5
            if color > 40:
                color = 5

            self.window.addstr(msg + "\n", curses.color_pair(color))

        except (KeyboardInterrupt, SystemExit):
            raise
        except:
            logging.error("curses handler error!")
            self.handleError(record)


class CursesUI(threading.Thread):
    def __init__(self, commander, screen, daemon=True):
        super(CursesUI, self).__init__(daemon=daemon)
        self.alive = True
        self.screen = screen
        self.commander = commander
        curses.start_color()
        curses.use_default_colors()
        num_colors = min(16, curses.COLORS)
        for i in range(0, num_colors):
            curses.init_pair(i+1, i, -1)
        curses.curs_set(0)
        curses.setsyx(-1, -1)
        self.screen.nodelay(1)

        maxy, maxx = self.screen.getmaxyx()
        self.width = maxx - 1
        begin_x = 0

        # Title Bar
        tbw_height = 2
        self.tb_window = curses.newwin(tbw_height, self.width, 0, begin_x)
        self.tb_window.scrollok(True)
        self.tb_window.idlok(True)
        self.tb_window.leaveok(True)
        title_str = "Commander Teensy v3"
        self.tb_window.addstr(0, (self.width - len(title_str)) // 2, title_str, curses.color_pair(3))

        # Main Window
        mw_height = 10
        self.main_window = curses.newwin(8, self.width, tbw_height, begin_x)
        self.main_window.scrollok(True)
        self.main_window.idlok(True)
        self.main_window.leaveok(True)
        # for i in range(num_colors):
        #     self.main_window.addstr(3, i, str(i), curses.color_pair(i))

        # Logging Window
        self.lw_height = maxy - mw_height - tbw_height
        self.log_window = curses.newwin(self.lw_height, self.width, tbw_height+mw_height, begin_x)
        self.log_window.scrollok(True)
        self.log_window.idlok(True)
        self.log_window.leaveok(True)
        mh = CursesHandler(self.log_window)

        mh.setFormatter('LogFile/'+logging.Formatter('|%(asctime)-8s|%(name)-10s|%(levelname)-7s|%(message)-s', '%H:%M:%S'))
        self.logger = logging.getLogger("")
        self.logger.addHandler(mh)

        self.start()

    def handle_packet(self, packet):
        try:
            w = self.main_window
            w.erase()
            if self.commander.serial_port != "DUMMY":
                w.addstr(0, 2, "Serial " + self.commander.serial_port)
            else:
                w.addstr(0, 2, "Serial " + self.commander.serial_port, curses.color_pair(5))
            status = self.commander.serial.is_open
            color = curses.color_pair(3) if status else curses.color_pair(6)
            status_txt = "OK" if status else "ERROR"
            w.addstr(0, 10+len(self.commander.serial_port), status_txt, color)

            w.addstr(1, 2, f"{self.commander.packets_per_second:06.1f} packets/s")

            # Raw arrivals vs. successfully unpacked packets; a growing gap between
            # the two means packets are coming in but not decoding.
            w.addstr(2, 2, f"{self.commander.serial_dump.n_raw_packets} received, "
                           f"{self.commander.n_packet} decoded")

            w.addstr(4, 2, f"PacketID {packet.packetID}")
            # w.refresh()
        except KeyboardInterrupt:
            logging.critical("INTERRUPT")
            self.alive = False
        except BaseException as e:
            logging.error(f"packet printing fail: {e}")

    def run(self):
        while self.alive:
            time.sleep(.1)
            time_str = datetime.now().strftime("%H:%M:%S %d.%m.%Y")
            self.tb_window.addstr(0, self.width-len(time_str), time_str)
            curses.noecho()
            if self.screen.getch() == ord('q'):
                logging.info("User requested exit.")
                self.alive = False
            # try:
            #     key = self.screen.getkey()
            #     logging.info(key)
            # except:
            #     pass

            # curses.echo()
            self.tb_window.refresh()
            self.main_window.refresh()
            self.log_window.border()
            self.log_window.refresh()


if __name__ == '__main__':
    screen = curses.initscr()
    gui = CursesUI(screen=screen, commander=None, daemon=False)
    curses.echo()
