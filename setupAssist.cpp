
// Assist setup for new app installations
// provided by gemi254

#include "myConfig.h"
#include <HTTPClient.h>

#define GITHUB_URL "https://raw.githubusercontent.com/s60sc/ESP32-CAM_MJPEG2SD/master/data/"

static const char* git_rootCACertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEsTCCA5mgAwIBAgIQBOHnpNxc8vNtwCtCuF0VnzANBgkqhkiG9w0BAQsFADBs\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j\n" \
"ZSBFViBSb290IENBMB4XDTEzMTAyMjEyMDAwMFoXDTI4MTAyMjEyMDAwMFowcDEL\n" \
"MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3\n" \
"LmRpZ2ljZXJ0LmNvbTEvMC0GA1UEAxMmRGlnaUNlcnQgU0hBMiBIaWdoIEFzc3Vy\n" \
"YW5jZSBTZXJ2ZXIgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC2\n" \
"4C/CJAbIbQRf1+8KZAayfSImZRauQkCbztyfn3YHPsMwVYcZuU+UDlqUH1VWtMIC\n" \
"Kq/QmO4LQNfE0DtyyBSe75CxEamu0si4QzrZCwvV1ZX1QK/IHe1NnF9Xt4ZQaJn1\n" \
"itrSxwUfqJfJ3KSxgoQtxq2lnMcZgqaFD15EWCo3j/018QsIJzJa9buLnqS9UdAn\n" \
"4t07QjOjBSjEuyjMmqwrIw14xnvmXnG3Sj4I+4G3FhahnSMSTeXXkgisdaScus0X\n" \
"sh5ENWV/UyU50RwKmmMbGZJ0aAo3wsJSSMs5WqK24V3B3aAguCGikyZvFEohQcft\n" \
"bZvySC/zA/WiaJJTL17jAgMBAAGjggFJMIIBRTASBgNVHRMBAf8ECDAGAQH/AgEA\n" \
"MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIw\n" \
"NAYIKwYBBQUHAQEEKDAmMCQGCCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2Vy\n" \
"dC5jb20wSwYDVR0fBEQwQjBAoD6gPIY6aHR0cDovL2NybDQuZGlnaWNlcnQuY29t\n" \
"L0RpZ2lDZXJ0SGlnaEFzc3VyYW5jZUVWUm9vdENBLmNybDA9BgNVHSAENjA0MDIG\n" \
"BFUdIAAwKjAoBggrBgEFBQcCARYcaHR0cHM6Ly93d3cuZGlnaWNlcnQuY29tL0NQ\n" \
"UzAdBgNVHQ4EFgQUUWj/kK8CB3U8zNllZGKiErhZcjswHwYDVR0jBBgwFoAUsT7D\n" \
"aQP4v0cB1JgmGggC72NkK8MwDQYJKoZIhvcNAQELBQADggEBABiKlYkD5m3fXPwd\n" \
"aOpKj4PWUS+Na0QWnqxj9dJubISZi6qBcYRb7TROsLd5kinMLYBq8I4g4Xmk/gNH\n" \
"E+r1hspZcX30BJZr01lYPf7TMSVcGDiEo+afgv2MW5gxTs14nhr9hctJqvIni5ly\n" \
"/D6q1UEL2tU2ob8cbkdJf17ZSHwD2f2LSaCYJkJA69aSEaRkCldUxPUd1gJea6zu\n" \
"xICaEnL6VpPX/78whQYwvwt/Tv9XBZ0k7YXDK/umdaisLRbvfXknsuvCnQsH6qqF\n" \
"0wGjIChBWUMo0oHjqvbsezt3tkBigAVBRQHvFwY+3sAzm2fTYS5yh+Rp/BIAV0Ae\n" \
"cPUeybQ=\n" \
"-----END CERTIFICATE-----\n";

void wgetFile(String url, String dir) {
  if (WiFi.status() != WL_CONNECTED) return;  
  String file_name = url.substring(url.lastIndexOf('/'));
  String ff = "" + dir + "" + file_name;
  //LOG_INF("Downloading %s",ff.c_str());
  File f = SD_MMC.open(ff, "w+");
  if ( f ){
    HTTPClient https;
    WiFiClientSecure wclient;
    wclient.setCACert(git_rootCACertificate);
    https.begin(wclient, url);
    LOG_INF("Downloading %s",ff.c_str());    
    int httpCode = https.GET();
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
      https.writeToStream(&f);
      f.close();
      LOG_INF("Downloaded %s", ff.c_str());        
    } else {
      f.close();
      SD_MMC.remove(ff);
      LOG_ERR("Download failed, error: %s", https.errorToString(httpCode).c_str());        
    }    
    https.end();
  } else LOG_ERR("Open failed: %s ", ff.c_str());
}

bool checkSDFiles() {
    //Check and download missing sd card files
    if (!SD_MMC.exists(DATA_DIR)) SD_MMC.mkdir(DATA_DIR);
    //No config map.. Download it
    File f = SD_MMC.open(CONFIG_FILE_PATH, FILE_READ);
    bool bConfigOK = f && f.size() > 0 ;
    f.close();
    if (!bConfigOK && WiFi.status() == WL_CONNECTED) {
      LOG_ERR("Invalid config file");
      wgetFile(GITHUB_URL "configs.txt", DATA_DIR);
      doRestart();
    }
    if (!SD_MMC.exists(INDEX_PAGE_PATH)) wgetFile(GITHUB_URL "MJPEG2SD.htm", DATA_DIR);      
    if (!SD_MMC.exists(DATA_DIR "/jquery.min.js")) wgetFile(GITHUB_URL "jquery.min.js", DATA_DIR);
    if (!SD_MMC.exists(DATA_DIR "/OTA.htm")) wgetFile(GITHUB_URL "OTA.htm", DATA_DIR);
    if (!SD_MMC.exists(DATA_DIR "/LOG.htm")) wgetFile(GITHUB_URL "LOG.htm", DATA_DIR);
    return true;
}

const char* defaultPage_html = R"~(
<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32-CAM_MJPEG2SD setup</title> 
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
    <tr><th colspan="3">ESP32-CAM_MJPEG2SD Wifi setup..</th></tr>
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
