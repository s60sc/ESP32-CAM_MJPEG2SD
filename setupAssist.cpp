
// Assist setup for new app installations
// original provided by gemi254

#include "globals.h"
#include <HTTPClient.h>

// DigiCert valid till 22/10/2028
const char* git_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIFYDCCBEigAwIBAgIQQAF3ITfU6UK47naqPGQKtzANBgkqhkiG9w0BAQsFADA/
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT
DkRTVCBSb290IENBIFgzMB4XDTIxMDEyMDE5MTQwM1oXDTI0MDkzMDE4MTQwM1ow
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQCt6CRz9BQ385ueK1coHIe+3LffOJCMbjzmV6B493XC
ov71am72AE8o295ohmxEk7axY/0UEmu/H9LqMZshftEzPLpI9d1537O4/xLxIZpL
wYqGcWlKZmZsj348cL+tKSIG8+TA5oCu4kuPt5l+lAOf00eXfJlII1PoOK5PCm+D
LtFJV4yAdLbaL9A4jXsDcCEbdfIwPPqPrt3aY6vrFk/CjhFLfs8L6P+1dy70sntK
4EwSJQxwjQMpoOFTJOwT2e4ZvxCzSow/iaNhUd6shweU9GNx7C7ib1uYgeGJXDR5
bHbvO5BieebbpJovJsXQEOEO3tkQjhb7t/eo98flAgeYjzYIlefiN5YNNnWe+w5y
sR2bvAP5SQXYgd0FtCrWQemsAXaVCg/Y39W9Eh81LygXbNKYwagJZHduRze6zqxZ
Xmidf3LWicUGQSk+WT7dJvUkyRGnWqNMQB9GoZm1pzpRboY7nn1ypxIFeFntPlF4
FQsDj43QLwWyPntKHEtzBRL8xurgUBN8Q5N0s8p0544fAQjQMNRbcTa0B7rBMDBc
SLeCO5imfWCKoqMpgsy6vYMEG6KDA0Gh1gXxG8K28Kh8hjtGqEgqiNx2mna/H2ql
PRmP6zjzZN7IKw0KKP/32+IVQtQi0Cdd4Xn+GOdwiK1O5tmLOsbdJ1Fu/7xk9TND
TwIDAQABo4IBRjCCAUIwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYw
SwYIKwYBBQUHAQEEPzA9MDsGCCsGAQUFBzAChi9odHRwOi8vYXBwcy5pZGVudHJ1
c3QuY29tL3Jvb3RzL2RzdHJvb3RjYXgzLnA3YzAfBgNVHSMEGDAWgBTEp7Gkeyxx
+tvhS5B1/8QVYIWJEDBUBgNVHSAETTBLMAgGBmeBDAECATA/BgsrBgEEAYLfEwEB
ATAwMC4GCCsGAQUFBwIBFiJodHRwOi8vY3BzLnJvb3QteDEubGV0c2VuY3J5cHQu
b3JnMDwGA1UdHwQ1MDMwMaAvoC2GK2h0dHA6Ly9jcmwuaWRlbnRydXN0LmNvbS9E
U1RST09UQ0FYM0NSTC5jcmwwHQYDVR0OBBYEFHm0WeZ7tuXkAXOACIjIGlj26Ztu
MA0GCSqGSIb3DQEBCwUAA4IBAQAKcwBslm7/DlLQrt2M51oGrS+o44+/yQoDFVDC
5WxCu2+b9LRPwkSICHXM6webFGJueN7sJ7o5XPWioW5WlHAQU7G75K/QosMrAdSW
9MUgNTP52GE24HGNtLi1qoJFlcDyqSMo59ahy2cI2qBDLKobkx/J3vWraV0T9VuG
WCLKTVXkcGdtwlfFRjlBz4pYg1htmf5X6DYO8A4jqv2Il9DjXA6USbW1FzXSLr9O
he8Y4IWS6wY7bCkjCWDcRQJMEhg76fsO3txE+FiYruq9RUWhiF1myv4Q6W+CyBFC
Dfvp7OOGAN6dEOM4+qR9sdjoSYKEBpsr6GtPAQw4dy753ec5
-----END CERTIFICATE-----
)~";

static fs::FS fp = STORAGE;

static void wgetFile(const char* githubURL, const char* filePath, bool restart = false) {
  if (!fp.exists(filePath)) {
    if (WiFi.status() != WL_CONNECTED) return;  
    char downloadURL[150];
    sprintf(downloadURL, "%s%s", githubURL, filePath);
////    for (int i = 0; i < 2; i++) { // secure not working
      for (int i = 1; i < 2; i++) { 
      // try secure then insecure
      File f = fp.open(filePath, FILE_WRITE);
      if (f) {
        HTTPClient https;
        WiFiClientSecure wclient;
        if (!i) wclient.setCACert(git_rootCACertificate);
        else wclient.setInsecure(); // not SSL      
        if (!https.begin(wclient, downloadURL)) {
          char errBuf[100];
          wclient.lastError(errBuf, 100);
          checkMemory();
          LOG_ERR("Could not connect to github server, err: %s", errBuf);
        } else {
          LOG_INF("Downloading %s from %s", filePath, downloadURL);    
          int httpCode = https.GET();
          int fileSize = 0;
          if (httpCode == HTTP_CODE_OK) {
            fileSize = https.writeToStream(&f);
            if (fileSize <= 0) {
              httpCode = 0;
              LOG_ERR("Download failed: writeToStream");
            } else LOG_INF("Downloaded %s, size %d bytes", filePath, fileSize);       
          } else LOG_ERR("Download failed, error: %s", https.errorToString(httpCode).c_str());    
          https.end();
          f.close();
          if (httpCode == HTTP_CODE_OK) break;
          else fp.remove(filePath);
        }
      } else LOG_ERR("Open failed: %s", filePath);
    } 
    if (restart) {
      loadConfig();
      doRestart("config file downloaded");
    }
  } 
}

bool checkDataFiles() {
  // Download any missing data files
  if (!fp.exists(DATA_DIR)) fp.mkdir(DATA_DIR);
  wgetFile(GITHUB_URL, CONFIG_FILE_PATH, true);
  wgetFile(GITHUB_URL, INDEX_PAGE_PATH);      
  wgetFile(GITHUB_URL, DATA_DIR "/OTA.htm");
  if (USE_LOG) wgetFile(GITHUB_URL, DATA_DIR "/LOG.htm");
  if (USE_WSL) wgetFile(GITHUB_URL, DATA_DIR "/WSL.htm");
  if (USE_JQUERY) wgetFile(GITHUB_URL, DATA_DIR "/jquery.min.js");
  if (USE_COMMON) wgetFile(GITHUB_URL, DATA_DIR "/common.js");
  if (USE_CONFIG) {
    wgetFile(GITHUB_URL, DATA_DIR "/CONFIG.htm");
    wgetFile(GITHUB_URL, DATA_DIR "/configs.js");
  }
  wgetFile(GITHUB_URL, DATA_DIR "/favicon.ico");
  return true;
}

const char* defaultPage_html = R"~(
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Application setup</title> 
</head>
<script>
function Config(){
  if (!window.confirm('This will reboot the device to activate new settings.'))  return false; 
  fetch('/control?ST_SSID=' + encodeURI(document.getElementById('ST_SSID').value))
  .then(r => { console.log(r); return fetch('/control?ST_Pass=' + encodeURI(document.getElementById('ST_Pass').value)) })
  .then(r => { console.log(r); return fetch('/control?save=1') })     
  .then(r => { console.log(r); return fetch('/control?reset=1') })
  .then(r => { console.log(r); }); 
  return false;
}
</script>
<body style="font-size:18px">
<br>
<center>
  <table border="0">
    <tr><th colspan="3">Wifi setup..</th></tr>
    <tr><td colspan="3"></td></tr>
    <tr>
    <td>SSID</td>
    <td>&nbsp;</td>
    <td><input id="ST_SSID" name="ST_SSID" length=32 placeholder="Router SSID" class="input"></td>
  </tr>
    <tr>
    <td>Password</td>
    <td>&nbsp;</td>
    <td><input id="ST_Pass" name="ST_Pass" length=64 placeholder="Router password" class="input"></td>
  </tr>
  <tr><td colspan="3"></td></tr>
    <tr><td colspan="3" align="center">
        <button type="button" onClick="return Config()">Connect</button>&nbsp;<button type="button" onclick="window.location.reload;">Cancel</button>
    </td></tr>
  </table>
</center>      
</body>
</html>
)~";
