# ESP32-CAM_MJPEG2SD

ESP32 / ESP32S3 Camera application to record JPEGs to SD card as AVI files and playback to browser as an MJPEG stream. The AVI format allows recordings to replay at correct frame rate on media players. If a microphone is installed then a WAV file is also created and stored in the AVI file.
 
Changes for version 8.0:
- compiled for arduino-esp32 v2.0.8
- support for ESP32S3 (much better than ESP32)
- simultaneous Wifi Station and AP mode
- lamp has variable intensity
- internal code restructuring.

Changes up to version 8.6.5:
- Web page improvements and jQuery removed.
- Support for OV5640 and OV3660 cameras, but see [**OV5640**](#ov5640) section below.
- Spurious error [message](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/155) removed. 
- fix for [timezone](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/150). 
- fix for unwanted [APs](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/144). 
- NTP server [configurable](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/151). 
- Valid GitHub cert for https download
- Improve AP stability
- Improve [PIR auto lamp](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/183)  
- MQTT integration added by [@gemi254](https://github.com/gemi254), see [**MQTT**](#mqtt) section below.
- Fix for [issue 198](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/198) 
- Added deep sleep on night, wake on LDR
- Fix for [issue 211](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/211)
- Fix for [issue 217](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/217)
- Web log colorised for different message types


## Purpose

The application enables video capture of motion detection or timelapse recording. Examples include security cameras or wildlife monitoring.  This [instructable](https://www.instructables.com/How-to-Make-a-WiFi-Security-Camera-ESP32-CAM-DIY-R/) by [Max Imagination](https://www.instructables.com/member/Max+Imagination/) shows how to build a WiFi Security Camera using an earlier version of this code, plus a later video on how to [install and use](https://www.youtube.com/watch?v=k_PJLkfqDuI&t=247s) the app.

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
XGA | 12.5 | 5 | 180
HD | 12.5 | 5 | 220
SXGA | 12.5 | 5 | 300
UXGA | 12.5 | 5 | 450

The ESP32S3 (using Freenove ESP32S3 Cam board hosting ESP32S3 N8R8 module) runs the app about double the speed of the ESP32 mainly due to much faster PSRAM. It can record at the maximum OV2640 frame rates including [audio](#audio-recording) for all frame sizes except UXGA (max 10fps).

## Design

The application was originally based on the Arduino CameraWebServer example but has since been extensively modified, including contributions made by [@gemi254](https://github.com/gemi254).

The ESP32 Cam module has 4MB of PSRAM which is used to buffer the camera frames and the construction of the AVI file to minimise the number of SD file writes, and optimise the writes by aligning them with the SD card sector size. For playback the AVI is read from SD into a multiple sector sized buffer, and sent to the browser as timed individual frames. The SD card is used in **MMC 1 line** mode, as this is practically as fast as **MMC 4 line** mode and frees up pin 4 (connected to onboard Lamp), and pin 12 which can be used for eg a PIR.  

The AVI files are named using a date time format **YYYYMMDD_HHMMSS** with added frame size, recording rate, duration and frame count, eg **20200130_201015_VGA_15_60_900.avi**, and stored in a per day folder **YYYYMMDD**. If audio is included the filename ends with **_S**.  
The ESP32 time is set from an NTP server or connected browser client.

## Installation

Download github files into the Arduino IDE sketch folder, removing `-master` from the application folder name.
Select the required ESP-CAM board using `CAMERA_MODEL_` in `appGlobals.h` unless using the defaults:
* ESP32 Cam board - `CAMERA_MODEL_AI_THINKER`
* ESP32S3 Cam board - `CAMERA_MODEL_ESP32S3_EYE`

Compile with PSRAM enabled and the following Partition scheme:
* ESP32 - `Minimal SPIFFS (...)`
* ESP32S3 - `8M with spiffs (...)`

**NOTE: If you get compilation errors you need to update your `arduino-esp32` library in the IDE 
using [Boards Manager](https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/61#issuecomment-1034928567)**

The application web pages and configuration data file (except passwords) are stored in the **/data** folder which needs to be copied as a folder to the SD card, or automatically downloaded from GitHub on app startup. This reduces the size of the application on flash and reduces wear as well as making updates easier.

On first installation, the application will start in wifi AP mode - connect to SSID: **ESP-CAM_MJPEG_...**, to allow router and password details to be entered via the web page on 192.168.4.1. The application web pages and configuration data file (except passwords) are stored in the **/data** folder which is automatically downloaded to SD card from GitHub. The **/data** folder can also be loaded via OTA.

Subsequent updates to the application, or to the **/data** folder contents, can be made using the **OTA Upload** tab. The **/data** folder can also be reloaded from GitHub using the **Reload /data** button on the **Edit Config** tab.

Browser functions only tested on Chrome.


## Main Function

A recording is generated either by the camera itself detecting motion as given in the [**Motion detection by Camera**](#motion-detection-by-camera) section below, or
by holding a given pin high (kept low by internal pulldown when released), eg by using a PIR.
In addition a recording can be requested manually using the **Start Recording** button on the web page.

To play back a recording, select the file using **Select folder / file** on the browser to select the day folder then the required AVI file.
After selecting the AVI file, press **Start Playback** button to playback the recording. 
The **Start Stream** button shows a live feed from the camera.

Recordings can then be uploaded to an FTP server or downloaded to the browser for playback on a media application, eg VLC.

A time lapse feature is also available which can run in parallel with motion capture. Time lapse files have the format **20200130_201015_VGA_15_60_900_T.avi**


## Other Functions and Configuration

The operation of the application can be modified dynamically as below, by using the main web page, which should mostly be self explanatory.

Connections:
* The FTP, Wifi, SMTP, and time zone parameters can be defined on the web page under **Other Settings**. 
  - for **Time Zone** use dropdown, or paste in values from second column [here](https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv)
* To make the changes persistent, press the **Save** button
* mdns name services in order to use `http://[Host Name]` instead of ip address.

To change the recording parameters:
* `Resolution` is the pixel size of each frame
* `Frame Rate` is the required frames per second
* `Quality` is the level of JPEG compression which affects image size.

SD storage management:
* Folders or files within folders can be deleted by selecting the required file or folder from the drop down list then pressing the **Delete** button and confirming.
* Folders or files within folders can be uploaded to a remote server via FTP by selecting the required file or folder from the drop down list then pressing the **FTP Upload** button. Can be uploaded in AVI format.
* Download selected AVI file from SD card to browser using **Download** button.
* Delete, or upload and delete oldest folder when card free space is running out.  
  
* Log viewing options via web page (may slow recorded frame rate), displayed using **Show Log** button:
  * **Log to browser**: log is dynamically output via websocket
  * **Log to SD card**: log is stored on SD card, use **Retrieve SD Log** button to retrieve or refresh.  


## Configuration Web Page

More configuration details accessed via **Edit Config** button, which displays further buttons:

**Wifi**:
Additional WiFi and webserver settings.

**Motion**: 
See [**Motion detection by Camera**](#motion-detection-by-camera) section.

**Peripherals** eg:
* Select if a PIR is to be used (which can also be used in parallel with camera motion detection).
* Auto switch the lamp on for nightime PIR detection.
* Control pan / tilt cradle for camera.
* Connect an external I2S microphone
* Connect a DS18B20 temperature sensor
* Monitor voltage of battery supply on ADC pin
* Wakeup on LDR after deep sleep at night

Note that there are not enough free pins on the ESP32 camera module to allow all external sensors to be used. Pins that can be used (with some limitations) are: 3, 4, 12, 13, 33.
* pin 3: Labelled U0R. Only use as input pin, as also used for flashing. 
* pin 4: Also used for onboard lamp. Lamp can be disabled by removing its current limiting resistor. 
* pin 12: Only use as output pin.
* pin 13: Is weakly pulled high.
* pin 33: Used by onboard red LED. Not broken out, but can repurpose the otherwise pointless VCC pin by removing its adjacent resistor marked 3V3, and the red LED current limiting resistor, then running a wire between the VCC pin and the red LED resistor solder tab.

Can also use the [ESP32-IO_Extender](https://github.com/s60sc/ESP32-IO_Extender) repository.  

The ESP32S3 Freenove board can support all of the above peripherals with its spare pins.

On-board LEDs:
* ESP32 - Lamp: 4, signal: 33.
* ESP32S3 - Lamp: 48, signal: 2.

**Other**:
SD and email management. An email can be sent when motion is detected.

When a feature is enable or disabled, the ESP should be rebooted using **Reboot ESP** button.


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

An I2S microphone can be supported, such as INMP441. PDM and analog microphones cannot be used due to limitations of I2S_NUM_1 peripheral. I2S_NUM_0 is not available as it is used by the camera. The audio is formatted as 16 bit single channel PCM with sample rate of 16kHz. The I2S microphone needs 3 free pins.

Audio recording works fine on ESP32S3 but is not viable on ESP32 as it significantly slows down the frame rate.

The web page has a slider for **Microphone Gain**. The higher the value the higher the gain. Selecting 0 cancels the microphone. Other settings under **Peripherals** button on the configuration web page.

## OV5640

The OV5640 pinout is compatible with boards designed for the OV2640 but the voltage supply is too high for the internal 1.5V regulator, so the camera overheats unless a heat sink is applied.

For recording purposes the OV5640 should only be used with an ESP32S3 board. Motion detection above `FHD` framesize does not work due to `esp_jpg_decode()` decompression [error](https://github.com/espressif/esp32-camera/issues/496).

Recordable frame rates for the OV5460 highest framesizes on an ESP32S3 are:

Frame Size | FPS 
------------ | -------------
QXSGA | 4
WQXGA | 5
QXGA | 5
QHD | 6
FHD | 6
P_FHD | 6

The OV3660 has not been tested.

## MQTT

To enable MQTT, under __Edit Config__ -> __Others__, enter fields:
* `Mqtt server ip to connect`
* `Mqtt topic path prefix`
* optionally `Mqtt user name` and `Mqtt user password`
* Then set `Mqtt enabled` 

Mqtt will auto connect if configuration is not blank on ping success.

It will send messages e.g. Record On/Off Motion On/Off to the mqtt broker on channel /status.  
topic: `homeassistant/sensor/ESP-CAM_MJPEG_904CAAF23A08/status -> {"MOTION":"ON", "TIME":"10:07:47.560"}`

You can also publish control commands to the /cmd channel in order to control camera.  
topic: `homeassistant/sensor/ESP-CAM_MJPEG_904CAAF23A08/cmd -> dbgVerbose=1;framesize=7;fps=1`