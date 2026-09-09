# Pin usage

The table below is the hand-maintained record of what each pad is wired to.
{doc}`totalsync-pinsheet` generates the same information as a `pinSheet.json`
straight from the firmware, and {doc}`totalsync-pinout` draws it onto the board:

```{image} images/pinout_example.png
:alt: A Teensy 4.1 pinout with every sampled pad labelled with its function and channel name
:width: 100%
```

DigitalInput | Used | For | DigitalOutput | Used | For | Analog | Used | For             
------------ | ------------- | ------------- | ------------- | ------------- | -------------  | -------------  | -------------  | -------------           
0 | Yes | Camera1 | 24 | Yes |  Valve | 16 | No | |
1 | Yes | Camera2 | 25 | Yes | Reward | 17 | Yes | Lick |
2 | Yes | Wheel encoder | 26 | Yes | Clue | 18 | No | |
3 | Yes | Wheel encoder | 27 | Yes | Lick detect | 19 | No | |
4 | Yes | Wheel encoder | 28 | No | | 20 | No | |
5 | No | | 29 | No | | 21 | No | |
6 | No | | 30 | No | | 22 | No | |
7 | No | | 31 | No | | 23 | No | |
8 | No | |
9 | No | |
10 | No | |
11 | No | | 
12 | No | |
13 | No | |
14 | No | |
15 | No | |
