# ESP32-CAM_MJPEG2SD

ESP32 Camera application to record JPEGs to SD card as MJPEG files and playback to browser. If a microphone is installed then a WAV file is also created - see  **Audio Recording** section below.

Files uploaded by FTP or downloaded from browser are optionally converted to AVI format to allow recordings to replay at correct frame rate on media players, including the audio if available.

 This [instructable](https://www.instructables.com/How-to-Make-a-WiFi-Security-Camera-ESP32-CAM-DIY-R/) by [Max Imagination](https://www.instructables.com/member/Max+Imagination/) shows how to build a WiFi Security Camera using an earlier version of this code.
 
 Version 5 of  this application has structural changes from the previous versions - see **Installation and Use** section.


## Purpose
The MJPEG format contains the original JPEG images but displays them as a video. MJPEG playback is not inherently rate controlled, but the app attempts to play back at the MJPEG recording rate. MJPEG files can also be played on video apps or converted into rate controlled AVI or MKV files etc.

Saving a set of JPEGs as a single file is faster than as individual files and is easier to manage, particularly for small image sizes. Actual rate depends on quality and size of SD card and complexity and quality of images. A no-name 4GB SDHC labelled as Class 6 was 3 times slower than a genuine Sandisk 4GB SDHC Class 2. The following recording rates were achieved on a freshly formatted Sandisk 4GB SDHC Class 2 on a AI Thinker OV2640 board, set to maximum JPEG quality and highest clock rate.

Frame Size | OV2640 camera max fps | mjpeg2sd max fps | Detection time ms
------------ | ------------- | ------------- | -------------
96X96 | 50 | 45 |  15
QQVGA | 50 | 45 |  20
QCIF  | 50 | 45 |  30
HQVGA | 50 | 45 |  40
240X240 | 50 | 45 |  55
QVGA | 50 | 40 |  70
CIF | 50 | 40 | 110
HVGA | 50 | 40 | 130
VGA | 25 | 20 |  80
SVGA | 25 | 20 | 120
XGA | 6.25 | 5 | 180
HD | 6.25 | 5 | 220
SXGA | 6.25 | 5 | 300
UXGA | 6.25 | 5 | 450

## Design

The application was originally based on the Arduino CameraWebServer example but has since been extensively modified, including contributions made by [@gemi254](https://github.com/gemi254).

The ESP32 Cam module has 4MB of pSRAM which is used to buffer the camera frames and the construction of the MJPEG file to minimise the number of SD file writes, and optimise the writes by aligning them with the SD card sector size. For playback the MJPEG is read from SD into a multiple sector sized buffer, and sent to the browser as timed individual frames. The SD card is used in **MMC 1 line** mode, as this is practically as fast as **MMC 4 line** mode and frees up pin 4 (connected to onboard Lamp), and pin 12 which can be used for eg a PIR.  

The MJPEG files are named using a date time format **YYYYMMDD_HHMMSS** with added frame size, recording rate, duration and frame count, eg **20200130_201015_VGA_15_60_900.mjpeg**, and stored in a per day folder **YYYYMMDD**.  
The ESP32 time is set from an NTP server or connected browser client.

## Installation

Download github files into the Arduino IDE sketch folder, removing `-master` from the application folder name.
Configure the application using the `#define` statements in `myConfig.h`, in particular select the required ESP-CAM board using `CAMERA_MODEL_` 
Compile with Partition Scheme: `Minimal SPIFFS (...)`.  and with PSRAM enabled.

**NOTE: If you get compilation errors you need to update your `arduino-esp32` library in the IDE 
using [Boards Manager](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/61#issuecomment-1034928567)**

The application web pages and configuration data file (except passwords) are stored in the **/data** folder which needs to be copied as a folder to the SD card.
This reduces the size of the application on flash and reduces wear as well as making updates easier.
Subsequent updates to the application, or to the **/data** folder contents, can be made using the **OTA Upload** button on the web page.

On first use, the application will start in wifi AP mode to allow router and other details to be entered via the web page, unless default values have been entered for the `ST_*` variables in `utils.cpp`.

Browser functions only tested on Chrome.


## Main Function

A recording is generated either by the camera itself detecting motion as given in the **Motion detection by Camera** section below, or
by holding a given pin high (kept low by internal pulldown when released), eg by using a PIR. The default is pin 12.
In addition a recording can be requested manually using the **Record** button on the web page.

To play back a recording, select the file using **Select folder / file** on the browser to select the day folder then the required MJPEG file.
After selecting the MJPEG file, press **Start Stream** button to playback the recording. 
After playback finished, press **Stop Stream** button. 
If a recording is started during a playback, playback will stop.
If recording occurs whilst also live streaming to browser, the frame rate will be slower. 

Recordings can then be uploaded to an FTP server or downloaded to the browser, selecting **Format as AVI** for playback on a media application, eg VLC.

A time lapse feature is also available which can run in parallel with motion capture. Time lapse files have the format **20200130_201015_VGA_15_TL_900.mjpeg**


## Other Functions and Configuration

Other functions of the application can be statically set using the `#define` statements in `myConfig.h`:
* Select if a PIR is to be used (which can also be used in parallel with camera motion detection).
* Auto switch the lamp on for nightime PIR detection.
* Connect an external I2S microphone
* Connect a DS18B20 temperature sensor
* Monitor voltage of battery supply  

Note that there are not enough free pins to allow all external sensors to be used. Pins that can be used (with some limitations) are: 4, 12, 3, 33.

The operation of the application can be modified dynamically as below, by using the web page, which should mostly be self explanatory.

Connections:
* The FTP, Wifi, and time zone parameters can be defined on the web page under **Other Settings**. 
* To make the changes persistent, press the **Save** button
* Press **Show Config** button to check that changes have been made.
* mdns name services in order to use `http://[Host Name]` instead of ip address.

To change the recording parameters:
* `Resolution` is the pixel size of each frame
* `Frame Rate` is the required frames per second
* `Quality` is the level of JPEG compression which affects image size.

SD storage management:
* Folders or files within folders can be deleted by selecting the required file or folder from the drop down list then pressing the **Delete** button and confirming.
* Folders or files within folders can be uploaded to a remote server via FTP by selecting the required file or folder from the drop down list then pressing the **FTP Upload** button. Can be uploaded in AVI format.
* Download selected MJPEG file from SD card to browser using **Download** button. Can be downloaded in AVI format.
* Delete, or upload and delete oldest folder when card free space is running out.  
  See `minCardFreeSpace` and `freeSpaceMode` in `myConfig.h`
  
* Log viewing options via web page **Log Mode** dropdown, in addition to serial port:
  * From SD card, view using **Show Log** button.
  * From remote host using `telnet [camera ip] 443` on remote client eg PuTTY


## Motion detection by Camera

An MJPEG recording can be generated by the camera itself detecting motion using the `motionDetect.cpp` file.  
JPEG images of any size are retrieved from the camera and 1 in N images are sampled on the fly for movement by decoding them to very small grayscale bitmap images which are compared to the previous sample. The small sizes provide smoothing to remove artefacts and reduce processing time.

For movement detection a high sample rate of 1 in 2 is used. When movement has been detected, the rate for checking for movement stop is reduced to 1 in 10 so that the JPEGs can be captured with only a small overhead. The **Detection time ms** table shows typical time in millis to decode and analyse a frame retrieved from the OV2640 camera.

Motion detection by camera is enabled by default, to disable click off **Enable motion detect** button on web page.

Additional options are provided on the camera index page, where:
* `Motion Sensitivity` sets a threshold for movement detection, higher is more sensitive.
* `Show Motion` if enabled and the **Start Stream** button pressed, shows images of how movement is detected for calibration purposes. Gray pixels show movement, which turn to black if the motion threshold is reached.
* `Min Frames` is the minimum number of frames to be captured or the file is deleted

![image1](extras/motion.png)

The `myConfig.h` file contains additional `#define` parameters that can be modified. 

## Audio Recording

The addition of a microphone significantly slows down the frame recording rate due to an unknown contention between the two I2S channels.

An I2S microphone can be supported, such as INMP441. PDM and analog microphones cannot be used due to limitations of I2S_NUM_1 peripheral. I2S_NUM_0 is not available as it is used by the camera. The audio is formatted as 16 bit single channel PCM with sample rate of 16kHz. The I2S microphone needs 3 free pins on the ESP32, selected from the following 4 pins:
- pin 3: Labelled U0R. Only use as input pin, i.e for microphone SD pin, as also used for flashing. Default microphone SD pin.
- pin 4: Also used for onboard lamp. Lamp can be disabled by removing its current limiting resistor. Default microphone SCK pin.
- pin 12: Only use as output pin, i.e for microphone WS or SCK pin. Default microphone WS pin.
- pin 33: Used by onboard red LED. Not broken out, but can repurpose the otherwise pointless VCC pin by removing its adjacent resistor marked 3V3 and the red LED current limiting resistor then running a wire between the VCC pin and the red LED resistor solder tab.

The web page has a slider for **Microphone Gain**. The higher the value the higher the gain. Selecting 0 cancels the microphone.

Refer to the file `myConfig.h` to define microphone pin assignment and for further info.
