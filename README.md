# ballin

**An open-source USB trackball powered by the RP2040.**

![](./imgs/IMG_1.jpg)

Uses [Jacek Fedorynski's](https://github.com/jfedor2) original PMW3360 trackball firmware and PCB [RP2040+PMW3360](https://github.com/jfedor2/rp2040-pmw3360)

3D models remixed from [here](https://www.printables.com/model/267954-trackball-15de/comments)

The firmware currently exposes seven physical buttons with this default mapping:

- `SIDE_MIDDLE`: Left Mouse Button
- `SIDE_RIGHT`: Right Mouse Button
- `SIDE_LEFT`: Mouse Middle Button
- `BOTTOM_LEFT`: Mouse 5 / Forward
- `BOTTOM_RIGHT`: Mouse 4 / Back
- `TOP_LEFT`: DPI down
- `TOP_RIGHT`: DPI up

Hardware scrolling is built into the firmware: press the left and right mouse buttons (`SIDE_MIDDLE` + `SIDE_RIGHT`) within `60ms` to enter scroll mode. While scrolling, left/right button events are suppressed; releasing either button exits scroll mode and temporarily locks left/right clicks until both buttons are fully released again.

### Scroll tuning
If you want to adjust the built-in scrolling behavior, the main knobs are:

- `ENABLE_HIRES_WHEEL` in [code/src/scroll.h](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.h): enables the high-resolution wheel descriptor and logic. Set this to `0` to fall back to the simpler legacy wheel behavior.
- `CHORD_MS` in [code/src/trackball.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/trackball.c): the maximum delay between left and right button presses to enter scroll mode.
- `SCROLL_BASE_BALL_UNITS_PER_DETENT` in [code/src/scroll.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.c): the main vertical scroll sensitivity. Larger values require more ball movement for one wheel step.
- `SCROLL_DEFAULT_MULTIPLIER` in [code/src/scroll.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.c): default HID wheel multiplier exposed to the host. `1` is the safest compatibility setting.
- `SCROLL_HORIZONTAL_DIVISOR` in [code/src/scroll.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.c): horizontal pan sensitivity. Larger values make side scrolling slower.
- `SCROLL_MAX_REPORT_WHEEL` in [code/src/scroll.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.c): caps how much vertical wheel movement can be sent in a single USB report, which helps avoid overly large jumps during fast flicks.
- `INVERT_VERTICAL_SCROLL` in [code/src/scroll.c](/home/harold/Workspace/trackball/ballin-rp2040-trackball/code/src/scroll.c): flips the vertical scroll direction if the current behavior feels backwards.

## Fabrication
Requires:

- 57.2mm billiard ball
- PMW3360 optical sensor
- PCB from here: https://github.com/jfedor2/rp2040-pmw3360
- 4 micro switches for mice
- 3 2.5mm zirconium oxide (or silicon nitride) bearing balls
- 2 M2x5 screws for securing the switch plates to base 
- 4 M3x4 screws for securing PCB to base
- 3 M3x12 flat head screws
- USB cable *Note: this should be a USB-A cable with the standard 4 wires. Using a USB-C cable might work, but since USB-C generally relies on a handshake before providing power, the trackball would not directly work if you plugged it into your computer with a USB-C cord unless you have a dongle in between.*

### 3D printing
I 3D printed the parts at .20 layer height, no supports needed. I used 40% sparse infill for the top part to get some rigidity and heft. The base I printed at standard 15% sparse infill. 

For the plates that hold the micro switchees, depending on your 3D printer's tolerances, the holes might not be big enough to fit the micro switch legs. I would suggest to first try widening them with some tweezers, if that doesn't work, adjust the X-Y hole compensation by + .1mm maybe. It's better for the holes to be tight and grip onto the switches well than to be too loose and have your switches wobble around. 

### Soldering button pins
Another thing to note is which pins on the PCB you solder your mouse buttons to. In the current compiled `trackball.uf2`, the buttons are defined like this in [code/src/trackball.c](https://github.com/SamIAm2000/ballin-rp2040-trackball/blob/main/code/src/trackball.c):
``` 
#define TOP_LEFT 16 // DPI down
#define TOP_RIGHT 28 // DPI up
#define BOTTOM_LEFT 17 // Mouse 5 / Forward
#define BOTTOM_RIGHT 26 // Mouse 4 / Back
#define SIDE_MIDDLE 18 // Mouse 1 / Left
#define SIDE_LEFT 19 // Mouse 3 / Middle
#define SIDE_RIGHT 20 // Mouse 2 / Right
```

![IMG_2.JPG](./imgs/IMG_2.JPG)

![IMG_3.JPG](./imgs/IMG_3.JPG)

## Compilation

You need Python and CMake installed. 

Once you've cloned this repository, run these commands in the ./code folder to create a build directory and compile.
```
mkdir build
cd build
cmake ..
make
```
The compiled file will be the `trackball.uf2` in the ./code/build/ folder.

## Flashing
To upload the compiled trackball.uf2 file from your computer to the trackball, plug in the USB cable from the trackball into your computer and hold boot, then press reset, then let go of both buttons. The trackball should appear in your list of devices. Drag and drop the .uf2 file on to the trackball.

## Demo
https://github.com/user-attachments/assets/3003e133-2ebc-4bb3-9a59-b019c4e8687f
