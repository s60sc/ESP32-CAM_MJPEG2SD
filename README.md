
# Websocket surveillance system

This is a modified version of ESP32-CAM_MJPEG2SD, implementing a **surveillance system** from multiple esp32-cam devices, using a web socket server on a **remote host** over the **internet**.

## Purpose

A **web-socket multi-client server** (websockets_stream_server.py) written in Python, acts as **Surveillance server**. It can be run on any remote host (Windows/Linux), allowing ep32 camera clients to connect and transmit their video feeds. Each ep32 camera act as a websocket client, making remote connections to the **remotet sockets server**. The server can be hosted over the internet, allowing video streams to be transmitted without any port/firewall restrictions. Multiple esp32-camera clients can be connected simultaneously on the server and all remote video streams can be viewed on a **single control page**. 

## Setup

On the remote host you will need to have **python 3** installed with some additional packages, and a free tcp port to make the connections (default is 9090). See /python_ backend/install.txt for information how to install stream server on the remote host. 

## How to use
After installing server configure each ESP32-CAM client. Navigate to camera interface `Edit config` > `Other` > `Websocket surveillance server` and enter the address of your server and connection port. Now press the `Websockets enabled` to enable remote connection on reboot and transmition of video feed to server.

On the remote server visit http://myserver.org:9090 to see all the videos streams from remote clients connected. Each video frame contains a **timestamp** of the local camera time, that is displayed as `video clock` with `hostname` at the top of the video feed. If **motion detection** is enabled on ESP32-CAM_MJPEG2SD, a motion message will be transmitted to the server and a **red box** will rendered on that video feed. 

Remote cameras also transmit other **information messages** like changes in the setup (framesize, lamp, fps) to the stream server. Theese messages and will be displayed in the bottom of each video stream allowing monitoring of each client. On mouse over each camera's `Remote query` input box, a text log will be displayed with all messages send / receive and on mouse over stream image an information text will be displayed as well.


# ESP32-CAM_MJPEG2SD

Visit https://github.com/s60sc/ESP32-CAM_MJPEG2SD for more details
