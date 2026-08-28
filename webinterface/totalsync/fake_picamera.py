"""
Create a fake picamera
"""
import time

import numpy as np


class BGR(object):
    """Fake class"""

    def __init__(self, sz):
        self.array = np.random.rand(*sz)

    def truncate(self, num):
        # refreshes the fake image
        self.array = np.random.rand(*self.array.shape)


class PiCamera:
    """Fake class"""
    resolution = (0, 0)

    def __init__(self, resolution=None, framerate=None, **options):
        # **options absorbs the rest of the real PiCamera signature (sensor_mode,
        # led_pin, ...); camera.py passes sensor_mode=, which would be a TypeError.
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def start_preview(self, **options):
        pass

    def stop_preview(self):
        pass

    def add_overlay(self, *args, **kwargs):
        pass

    def remove_overlay(self, *args, **kwargs):
        pass

    def capture(self, output, format=None, use_video_port=False, resize=None, splitter_port=0, bayer=False, **options):
        raise NotImplementedError

    def start_recording(self, output, format=None, resize=None, splitter_port=1, **options):
        pass

    def split_recording(self, *args, **kwargs):
        pass

    def wait_recording(self, timeout=0, splitter_port=1):
        if timeout:
            time.sleep(timeout)

    def stop_recording(self):
        pass

    def record_sequence(self, *args, **kwargs):
        raise NotImplementedError

    def capture_sequence(self, *args, **kwargs):
        raise NotImplementedError

    def capture_continuous(self, *args, **kwargs):
        raise NotImplementedError

    def close(self):
        pass


class array:
    """Fake class"""

    @staticmethod
    def PiRGBArray(cam, size):
        return BGR(size)
