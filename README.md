# ESP32-CAM_MJPEG2SD

ESP32 Camera application to record JPEGs to SD card as AVI files and playback to browser as an MJPEG stream. The AVI format allows recordings to replay at correct frame rate on media players. If a microphone is installed then a WAV file is also created and stored in the AVI file.
 
Changes from previous version 6:
* Configuration changed dynamically via browser instead of compile time defines.
* Remote logging via web page rather than telnet.
* Peripherals can be hosted on a separate ESP.
* Optional login for main web page.
* /data folder needs to be reloaded

## Purpose

The application enables video capture of motion detection or timelapse recording. Examples include security cameras or wildlife monitoring.  This [instructable](https://www.instructables.com/How-to-Make-a-WiFi-Security-Camera-ESP32-CAM-DIY-R/) by [Max Imagination](https://www.instructables.com/member/Max+Imagination/) shows how to build a WiFi Security Camera using an earlier version of this code.

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

The ESP32 Cam module has 4MB of pSRAM which is used to buffer the camera frames and the construction of the AVI file to minimise the number of SD file writes, and optimise the writes by aligning them with the SD card sector size. For playback the AVI is read from SD into a multiple sector sized buffer, and sent to the browser as timed individual frames. The SD card is used in **MMC 1 line** mode, as this is practically as fast as **MMC 4 line** mode and frees up pin 4 (connected to onboard Lamp), and pin 12 which can be used for eg a PIR.  

The AVI files are named using a date time format **YYYYMMDD_HHMMSS** with added frame size, recording rate, duration and frame count, eg **20200130_201015_VGA_15_60_900.avi**, and stored in a per day folder **YYYYMMDD**. If audio is included the filename ends with **_S**.  
The ESP32 time is set from an NTP server or connected browser client.

## Installation

Download github files into the Arduino IDE sketch folder, removing `-master` from the application folder name.
Select the required ESP-CAM board using `CAMERA_MODEL_` in `globals.h` 
Compile with Partition Scheme: `Minimal SPIFFS (...)`.  and with PSRAM enabled.

**NOTE: If you get compilation errors you need to update your `arduino-esp32` library in the IDE 
using [Boards Manager](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/61#issuecomment-1034928567)**

The application web pages and configuration data file (except passwords) are stored in the **/data** folder which needs to be copied as a folder to the SD card, or automatically downloaded from GitHub on app startup. This reduces the size of the application on flash and reduces wear as well as making updates easier.

On first use, the application will start in wifi AP mode to allow router and other details to be entered via the web page. If the **/data** folder is not present on the SD card, it is downloaded from GitHub.
Subsequent updates to the application, or to the **/data** folder contents, can be made using the **OTA Upload** button on the main web page. The **/data** folder can also be reloaded from GitHub using the **Reload /data** button on the configuration web page accessed via the **Edit Config** button.

Browser functions only tested on Chrome.


## Main Function

A recording is generated either by the camera itself detecting motion as given in the **Motion detection by Camera** section below, or
by holding a given pin high (kept low by internal pulldown when released), eg by using a PIR.
In addition a recording can be requested manually using the **Record** button on the web page.

To play back a recording, select the file using **Select folder / file** on the browser to select the day folder then the required AVI file.
After selecting the AVI file, press **Start Stream** button to playback the recording. 
After playback finished, press **Stop Stream** button. 
If a recording is started during a playback, playback will stop.
If recording occurs whilst also live streaming to browser, the frame rate will be slower. 

Recordings can then be uploaded to an FTP server or downloaded to the browser for playback on a media application, eg VLC.

A time lapse feature is also available which can run in parallel with motion capture. Time lapse files have the format **20200130_201015_VGA_15_60_900_T.avi**


## Other Functions and Configuration

The operation of the application can be modified dynamically as below, by using the main web page, which should mostly be self explanatory.

Connections:
* The FTP, Wifi, SMTP, and time zone parameters can be defined on the web page under **Other Settings**. 
* To make the changes persistent, press the **Save** button
* mdns name services in order to use `http://[Host Name]` instead of ip address.

To change the recording parameters:
* `Resolution` is the pixel size of each frame
* `Frame Rate` is the required frames per second
* `Quality` is the level of JPEG compression which affects image size.

SD storage management:
* Folders or files within folders can be deleted by selecting the required file or folder from the drop down list then pressing the **Delete** button and confirming.
* Folders or files within folders can be uploaded to a remote server via FTP by selecting the required file or folder from the drop down list then pressing the **FTP Upload** button. Can be uploaded in AVI format.
* Download selected AVI file from SD card to browser using **Download** button. Can be downloaded in AVI format.
* Delete, or upload and delete oldest folder when card free space is running out.  
  
* Log viewing options via web page **Log to SD** slider, displayed using **Show Log** button, in addition to serial port:
  * On: log is saved on SD card 
  * Off: log is dynamically output via websocket.

## Configuration Web Page

More configuration details accessed via **Edit Config** button, which displays further buttons:

**Wifi**:
Additional WiFi and webserver settings.

**Motion**: 
See **Motion detection by Camera** section

**Peripherals** eg:
* Select if a PIR is to be used (which can also be used in parallel with camera motion detection).
* Auto switch the lamp on for nightime PIR detection.
* Connect an external I2S microphone
* Connect a DS18B20 temperature sensor
* Monitor voltage of battery supply

Note that there are not enough free pins on the camera module to allow all external sensors to be used. Pins that can be used (with some limitations) are: 4, 12, 3, 33.
Can also use the [ESP32-IO_Extender](https://github.com/s60sc/ESP32-IO_Extender) repository.

**Other**:
SD and email management.

When a feature is enable or disabled, the ESP should be rebooted.


## Motion detection by Camera

An AVI recording can be generated by the camera itself detecting motion using the `motionDetect.cpp` file.  
JPEG images of any size are retrieved from the camera and 1 in N images are sampled on the fly for movement by decoding them to very small grayscale bitmap images which are compared to the previous sample. The small sizes provide smoothing to remove artefacts and reduce processing time.

For movement detection a high sample rate of 1 in 2 is used. When movement has been detected, the rate for checking for movement stop is reduced to 1 in 10 so that the JPEGs can be captured with only a small overhead. The **Detection time ms** table shows typical time in millis to decode and analyse a frame retrieved from the OV2640 camera.

Motion detection by camera is enabled by default, to disable click off **Enable motion detect** button on web page.

Additional options are provided on the camera index page, where:
* `Motion Sensitivity` sets a threshold for movement detection, higher is more sensitive.
* `Show Motion` if enabled and the **Start Stream** button pressed, shows images of how movement is detected for calibration purposes. Gray pixels show movement, which turn to black if the motion threshold is reached.
* `Min Frames` is the minimum number of frames to be captured or the file is deleted

![image1](extras/motion.png)
 

## Audio Recording

The addition of a microphone significantly slows down the frame recording rate due to an unknown contention between the two I2S channels, which also degrades the audio quality.

An I2S microphone can be supported, such as INMP441. PDM and analog microphones cannot be used due to limitations of I2S_NUM_1 peripheral. I2S_NUM_0 is not available as it is used by the camera. The audio is formatted as 16 bit single channel PCM with sample rate of 16kHz. The I2S microphone needs 3 free pins on the ESP32, selected from the following 4 pins:
- pin 3: Labelled U0R. Only use as input pin, i.e for microphone SD pin, as also used for flashing. Default microphone SD pin.
- pin 4: Also used for onboard lamp. Lamp can be disabled by removing its current limiting resistor. Default microphone SCK pin.
- pin 12: Only use as output pin, i.e for microphone WS or SCK pin. Default microphone WS pin.
- pin 33: Used by onboard red LED. Not broken out, but can repurpose the otherwise pointless VCC pin by removing its adjacent resistor marked 3V3 and the red LED current limiting resistor then running a wire between the VCC pin and the red LED resistor solder tab.

The web page has a slider for **Microphone Gain**. The higher the value the higher the gain. Selecting 0 cancels the microphone. Other settings under **Peripherals** button on the configuration web page.

