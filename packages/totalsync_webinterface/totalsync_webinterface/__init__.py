"""Serial reader and browser interface for the TotalSync Teensy synchroniser.

The `totalsync` command is `teensy_commander.cli_entry`: it reads packets off the
Teensy's serial port and serves them to the browser interface in `web/`, which this
package ships as package data (see `web_interface.WEB_DIRECTORY`).
"""

__version__ = '0.1.0'
