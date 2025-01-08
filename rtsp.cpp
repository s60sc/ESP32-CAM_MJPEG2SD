// Library can be found here https://github.com/rjsachse/ESP32-RTSPServer.git or in Arduino library

  // Initialize the RTSP server
  /**
   * @brief Initializes the RTSP server with the specified configuration.
   * 
   * This method can be called with specific parameters, or the parameters
   * can be set directly in the RTSPServer instance before calling begin().
   * If any parameter is not explicitly set, the method uses default values.
   * 
   * @param transport The transport type. Default is VIDEO_AND_SUBTITLES. Options are (VIDEO_ONLY, AUDIO_ONLY, VIDEO_AND_AUDIO, VIDEO_AND_SUBTITLES, AUDIO_AND_SUBTITLES, VIDEO_AUDIO_SUBTITLES).
   * @param rtspPort The RTSP port to use. Default is 554.
   * @param sampleRate The sample rate for audio streaming. Default is 0 must pass or set if using audio.
   * @param port1 The first port (used for video, audio or subtitles depending on transport). Default is 5430.
   * @param port2 The second port (used for audio or subtitles depending on transport). Default is 5432.
   * @param port3 The third port (used for subtitles). Default is 5434.
   * @param rtpIp The IP address for RTP multicast streaming. Default is IPAddress(239, 255, 0, 1).
   * @param rtpTTL The TTL value for RTP multicast packets. Default is 64.
   * @return true if initialization is successful, false otherwise.
   */
// RjSachse 2025
#include "appGlobals.h"

#if INCLUDE_RTSP

RTSPServer rtspServer;

int quality; //Variable to hold quality for RTSP frame
bool rtspVideo;
bool rtspAudio;
bool rtspSubtitles;
int rtspPort;
uint16_t rtpVideoPort;
uint16_t rtpAudioPort;
uint16_t rtpSubtitlesPort;
char RTP_ip [MAX_IP_LEN];
uint8_t rtpTTL;

IPAddress rtpIp;
char transportStr[30];  // Adjust the size as needed

RTSPServer::TransportType determineTransportType() { 
  if (rtspVideo && rtspAudio && rtspSubtitles) { 
    strcpy(transportStr, "s: Video, Audio & Subtitles");
    return RTSPServer::VIDEO_AUDIO_SUBTITLES; 
  } else if (rtspVideo && rtspAudio) { 
    strcpy(transportStr, "s: Video & Audio");
    return RTSPServer::VIDEO_AND_AUDIO; 
  } else if (rtspVideo && rtspSubtitles) { 
    strcpy(transportStr, "s: Video & Subtitles");
    return RTSPServer::VIDEO_AND_SUBTITLES; 
  } else if (rtspAudio && rtspSubtitles) { 
    strcpy(transportStr, "s: Audio & Subtitles");
    return RTSPServer::AUDIO_AND_SUBTITLES; 
  } else if (rtspVideo) { 
    strcpy(transportStr, ": Video");
    return RTSPServer::VIDEO_ONLY; 
  } else if (rtspAudio) { 
    strcpy(transportStr, ": Audio");
    return RTSPServer::AUDIO_ONLY; 
  } else if (rtspSubtitles) { 
    strcpy(transportStr, ": Subtitles");
    return RTSPServer::SUBTITLES_ONLY; 
  } else { 
    strcpy(transportStr, ": None!");
    return RTSPServer::NONE; 
  }
}

void sendRTSPSubtitles(void* arg) { 
  char data[100];
  time_t currEpoch = getEpoch();
  size_t len = strftime(data, 12, "%H:%M:%S  ", localtime(&currEpoch));
  len += sprintf(data + len, "FPS: %lu", rtspServer.rtpFps);
#if INCLUDE_TELEM
  // add telemetry data 
  if (teleUse) {
    storeSensorData(true);
    if (srtBytes) len += sprintf(data + len, "%s"(const char*)srtBuffer);
    srtBytes = 0;
  }
#endif
  rtspServer.sendRTSPSubtitles(data, len);
}

void prepRTSP() {
  RTSPServer::TransportType transport = determineTransportType();
  rtpIp.fromString(RTP_ip);
  rtspServer.transport = transport;
#if INCLUDE_AUDIO
  rtspServer.sampleRate = SAMPLE_RATE; 
#endif
  rtspServer.rtspPort = rtspPort; 
  rtspServer.rtpVideoPort = rtpVideoPort; 
  rtspServer.rtpAudioPort = rtpAudioPort; 
  rtspServer.rtpSubtitlesPort = rtpSubtitlesPort;
  rtspServer.rtpIp = rtpIp; 
  rtspServer.rtpTTL = rtpTTL; 
    
  if (transport != RTSPServer::NONE) {
    if (rtspServer.init()) { 
      LOG_INF("RTSP server started successfully with transport%s, Connect to: rtsp://%s:%d", transportStr, WiFi.localIP().toString().c_str(), rtspServer.rtspPort);
      if (transport != RTSPServer::AUDIO_ONLY && transport != RTSPServer::VIDEO_ONLY && transport != RTSPServer::VIDEO_AND_AUDIO) rtspServer.startSubtitlesTimer(sendRTSPSubtitles); // 1-second period
    } else { 
      LOG_ERR("Failed to start RTSP server"); 
    }
  } else {
    LOG_WRN("RTSP server not started, no transport selected");
  }
}

#endif