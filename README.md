
This ia a modified version from https://github.com/s60sc/ESP32-CAM_MJPEG2SD
Added functionality 

* Upload a folder or file to an ftp server creating necessarily dirs
* Added Utils section with reboot button
* Remove file or folder from sd card 
* Add software motion detection to enable recording
* Create a file myConfig.h and add you custom config values

# ESP32-CAM_MJPEG2SD
ESP32 Camera extension to record JPEGs to SD card as MJPEG files and playback to browser 

## Purpose
The MJPEG format contains the original JPEG images but displays them as a video. MJPEG playback is not inherently rate controlled, but the app attempts to play back at the MJPEG recording rate. MJPEG files can also be played on video apps or converted into rate controlled AVI or MKV files etc.

Saving a set of JPEGs as a single file is faster than as individual files and is easier to manage, particularly for small image sizes. Actual rate depends on quality and size of SD card and complexity and quality of images. A no-name 4GB SDHC labelled as Class 6 was 3 times slower than a genuine Sandisk 4GB SDHC Class 2. The following recording rates were achieved on a freshly formatted Sandisk 4GB SDHC Class 2 using SD_MMC 1 line mode on a AI Thinker OV2640 board, set to maximum JPEG quality and the configuration given in the __To maximise rate__ section below.

Frame Size | OV2640 camera max fps | mjpeg2sd max fps
------------ | ------------- | -------------
QQVGA | 50 | 35 
HQVGA | 50 | 30
QVGA | 50 | 25
CIF | 50 | 20
VGA | 25 | 15
SVGA | 25 | 10
XGA | 6.25 | 6
SXGA | 6.25 | 4
UXGA | 6.25 | 2

## Design

The ESP32 Cam module has 4MB of pSRAM which is used to buffer the camera frames and the construction of the MJPEG file to minimise the number of SD file writes, and optimise the writes by aligning them with the SD card cluster size. For playback the MJPEG is read from SD into a cluster sized buffer, and sent to the browser as timed individual frames.

The SD card can be used in either __MMC 1 line__ mode (default) or __MMC 4 line__ mode. The __MMC 1 line__ mode is practically as fast as __MMC 4 line__ and frees up pin 4 (connected to onboard Lamp), and pin 12 which can be used for eg a PIR.  

The MJPEG files are named using a date time format __YYYYMMDD_HHMMSS__, with added frame size, recording rate and duration, eg __20200130_201015_VGA_15_60.mjpeg___, and stored in a per day folder __YYYYMMDD__.  
The ESP32 time is set from an NTP server. Define a different timezone as appropriate in`mjpeg2sd.cpp`.


## Installation and Use

Download files into the Arduino IDE sketch location, removing `-master` from the folder name.  
The included sketch `ESP32-CAM_MJPEG2SD.ino` is derived from the `CameraWebServer.ino` example sketch included in the Arduino ESP32 library. 
Additional code has been added to the original file `app_httpd.cpp` to handle the extra browser options, and an additional file`mjpeg2sd.cpp` contains the SD handling code. The web page content in `camera_index.h` has been updated to include additional functions. 
The face detection code has been removed to reduce the sketch size to allow OTA updates.

To set the recording parameters, additional options are provided on the camera index page, where:
* `Frame Rate` is the required frames per second
* `Min Frames` is the minimum number of frames to be captured or the file is deleted
* `Verbose` if checked outputs additional logging to the serial monitor

An MJPEG recording is generated by holding a given pin high (kept low by internal pulldown when released).  
The pin to use is:
* pin 12 when in 1 line mode
* pin 33 when in 4 line mode

An MJPEG recording can also be generated by the camera itself detecting motion, by adding the file `motionDetect.cpp` from the [ESP32-CAM_Motion](https://github.com/s60sc/ESP32-CAM_Motion) repository to this sketch folder, then in `mjpeg2sd.cpp` set `#define USE_MOTION true`

If recording occurs whilst also live streaming to browser, the frame rate will be slower. 

To play back a recording, select the file using __Select folder / file__ on the browser to select the day folder then the required MJPEG file.
After selecting the MJPEG file, press __Start Stream__ button to playback the recording. 
The recorded playback rate can be changed during replay by changing the __FPS__ value. 
After playback finished, press __Stop Stream__ button. 
If a recording is started during a playback, playback will stop.

Browser functions only tested on Chrome.


## To maximise rate

To get the maximum frame rate on OV2640, in `ESP32-CAM_MJPEG2SD.ino`:
* `config.xclk_freq_hz = 10000000;` This is faster than the default `20000000` 
* `config.fb_count = 8` to provide sufficient buffering between SD writes for smaller frame sizes 

In `mjpeg2sd.cpp` change `#define CLUSTERSIZE 32768` if the SD card cluster size is not 32kB.

