try:
    from setuptools import setup
except ImportError:
    from distutils.core import setup


setup(name='Totalsync',
      description='Synchronization system',
      author='Ronny Eichler and Morgane Audrain',
      author_email='morganeaudrain@gmail.com',
      version=3,
      license='',
      install_requires=['cobs',
                        'pyserial',
                        'numpy',
                        'pyzmq',
                        'pyglet',
                        'sounddevice',
                        'windows-curses ; platform_system == "Windows"'],
      packages=['webinterface.totalsync'],
      entry_points="""[console_scripts]
            totalsync=webinterface.totalsync.TeensyCommander:cli_entry"""
      )
