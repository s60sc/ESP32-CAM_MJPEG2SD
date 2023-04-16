
// Assist setup for new app installations
// original provided by gemi254
//
// s60sc 2023

#include "appGlobals.h"

// Cert valid till April 2031
const char* git_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIEvjCCA6agAwIBAgIQBtjZBNVYQ0b2ii+nVCJ+xDANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0yMTA0MTQwMDAwMDBaFw0zMTA0MTMyMzU5NTlaME8xCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxKTAnBgNVBAMTIERpZ2lDZXJ0IFRMUyBS
U0EgU0hBMjU2IDIwMjAgQ0ExMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC
AQEAwUuzZUdwvN1PWNvsnO3DZuUfMRNUrUpmRh8sCuxkB+Uu3Ny5CiDt3+PE0J6a
qXodgojlEVbbHp9YwlHnLDQNLtKS4VbL8Xlfs7uHyiUDe5pSQWYQYE9XE0nw6Ddn
g9/n00tnTCJRpt8OmRDtV1F0JuJ9x8piLhMbfyOIJVNvwTRYAIuE//i+p1hJInuW
raKImxW8oHzf6VGo1bDtN+I2tIJLYrVJmuzHZ9bjPvXj1hJeRPG/cUJ9WIQDgLGB
Afr5yjK7tI4nhyfFK3TUqNaX3sNk+crOU6JWvHgXjkkDKa77SU+kFbnO8lwZV21r
eacroicgE7XQPUDTITAHk+qZ9QIDAQABo4IBgjCCAX4wEgYDVR0TAQH/BAgwBgEB
/wIBADAdBgNVHQ4EFgQUt2ui6qiqhIx56rTaD5iyxZV2ufQwHwYDVR0jBBgwFoAU
A95QNVbRTLtm8KPiGxvDl7I90VUwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQG
CCsGAQUFBwMBBggrBgEFBQcDAjB2BggrBgEFBQcBAQRqMGgwJAYIKwYBBQUHMAGG
GGh0dHA6Ly9vY3NwLmRpZ2ljZXJ0LmNvbTBABggrBgEFBQcwAoY0aHR0cDovL2Nh
Y2VydHMuZGlnaWNlcnQuY29tL0RpZ2lDZXJ0R2xvYmFsUm9vdENBLmNydDBCBgNV
HR8EOzA5MDegNaAzhjFodHRwOi8vY3JsMy5kaWdpY2VydC5jb20vRGlnaUNlcnRH
bG9iYWxSb290Q0EuY3JsMD0GA1UdIAQ2MDQwCwYJYIZIAYb9bAIBMAcGBWeBDAEB
MAgGBmeBDAECATAIBgZngQwBAgIwCAYGZ4EMAQIDMA0GCSqGSIb3DQEBCwUAA4IB
AQCAMs5eC91uWg0Kr+HWhMvAjvqFcO3aXbMM9yt1QP6FCvrzMXi3cEsaiVi6gL3z
ax3pfs8LulicWdSQ0/1s/dCYbbdxglvPbQtaCdB73sRD2Cqk3p5BJl+7j5nL3a7h
qG+fh/50tx8bIKuxT8b1Z11dmzzp/2n3YWzW2fP9NsarA4h20ksudYbj/NhVfSbC
EXffPgK2fPOre3qGNm+499iTcc+G33Mw+nur7SpZyEKEOxEXGlLzyQ4UfaJbcme6
ce1XR2bFuAJKZTRei9AqPCCcUZlM51Ke92sRKw2Sfh3oius2FkOH6ipjv3U/697E
A7sKPPcw7+uvTPyLNhBzPvOk
-----END CERTIFICATE-----
)~";

static fs::FS fp = STORAGE;

static bool wgetFile(const char* githubURL, const char* filePath, bool restart = false) {
  // download file from github
  if (fp.exists(filePath)) {
    // if file exists but is empty, delete it to allow re-download
    File f = fp.open(filePath, FILE_READ);
    size_t fSize = f.size();
    f.close();
    if (!fSize) fp.remove(filePath);
  }
  if (!fp.exists(filePath)) {
    if (WiFi.status() != WL_CONNECTED) return false;  
    char downloadURL[150];
    snprintf(downloadURL, 150, "%s%s", githubURL, filePath);
    for (int i = 0; i < 2; i++) { 
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
      } else {
        LOG_ERR("Open failed: %s", filePath);
        return false;
      }
    } 
    if (restart) {
      if (loadConfig()) doRestart("config file downloaded");
    }
  } 
  return true;
}

bool checkDataFiles() {
  // Download any missing data files
  if (!fp.exists(DATA_DIR)) fp.mkdir(DATA_DIR);
  bool res = false;
  if (strlen(GITHUB_URL)) {
    res = wgetFile(GITHUB_URL, CONFIG_FILE_PATH, true);
    if (res) res = wgetFile(GITHUB_URL, INDEX_PAGE_PATH);      
    if (res) res = appDataFiles();
  }
  return res;
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
  <br/><br/><a href="/web?OTA.htm"><button>OTA Update</button></a>
</center>      
</body>
</html>
)~";

// in case app html is not present, or corrupted
// <ip address>/web?OTA.htm
const char* otaPage_html = R"~(
<html>
  <head>
    <title>Built In OTA</title>
  </head>
  <body>
    <br></br>
    <form id="upload_form" enctype="multipart/form-data" method="post">
      <input type="file" name="file1" id="file1" onchange="uploadFile()"><br>
      <br></br>
      <progress id="progressBar" value="0" max="100" style="width:300px;"></progress>
      <h3 id="status"></h3>
      <p id="loaded_n_total"></p>
    </form>
    <script>
      const otaPort = 82;
      const otaServer = 'http://' + document.location.hostname + ':' + otaPort;
      const $ = document.querySelector.bind(document);
     
      async function uploadFile() {
        // notify server to start ota task
        const response = await fetch('/control?startOTA=1');
        if (response.ok) {
          // submit file for uploading
          let file = $("#file1").files[0];
          let formdata = new FormData();
          formdata.append("file1", file);
          let ajax = new XMLHttpRequest();
          ajax.upload.addEventListener("progress", progressHandler, false);
          ajax.addEventListener("load", completeHandler, false);
          ajax.addEventListener("error", errorHandler, false);
          ajax.addEventListener("abort", abortHandler, false);
          ajax.open("POST", otaServer + '/upload');
          ajax.send(formdata);
        } else console.log(response.status); 
      }

       function progressHandler(event) {
        $("#loaded_n_total").innerHTML = "Uploaded " + event.loaded + " of " + event.total + " bytes";
        let percent = (event.loaded / event.total) * 100;
        $("#progressBar").value = Math.round(percent);
        $("#status").innerHTML = Math.round(percent) + "% transferred";
        if (event.loaded  == event.total) $("#status").innerHTML = 'Uploaded, wait for completion result';
      }

      function completeHandler(event) {
        $("#status").innerHTML = event.target.responseText;
      }

      function errorHandler(event) {
        $("#status").innerHTML = "Upload Failed";
      }

      function abortHandler(event) {
        $("#status").innerHTML = "Upload Aborted";
      }
    </script>
  </body>
</html>
)~";
