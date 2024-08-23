
/*  
  Using the WebDAV server:
    Windows 10:
    - Windows file explorer, in address bar enter: <ip_address>/webdav
    - Map Network Drive, connect to: \\<ip_address>\webdav
    Windows 11:
    - Map Network Drive:
      - connect to: \\<ip_address>\webdav
      - Click on the link “Connect to a web site that you can use to store your documents and pictures.”
      - Click “Next” and then “Choose a custom network location.”
      - Re-enter \\<ip_address>\webdav

    Android:
    - Solid Explorer, enter <ip_address> for Remote host name, webdav for Path

  Not tested:
    MacOS:
    - Finder: command-K > http://<ip_address>/webdav (do not select anonymous to have write access)
    - cmdline: mkdir -p /tmp/esp; mount_webdav -S -i -v esp32 <ip_address>/webdav /tmp/esp && echo OK
 
    linux:
    - mount -t davs2 http://<ip_address>/webdav /mnt/
    - gio/gvfs/nautilus/YourFileExplorer http://<ip_address>/webdav

  Uses ideas from https://github.com/d-a-v/ESPWebDAV
   
  s60sc 2024
*/

#include "appGlobals.h"

#if INCLUDE_WEBDAV
#define ALLOW "PROPPATCH,PROPFIND,OPTIONS,DELETE,MOVE,COPY,HEAD,POST,PUT,GET"
#define XML1 "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\">"
#define XML2 "<D:response xmlns:D=\"DAV:\"><D:href>"
#define XML3 "</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop>"
#define XML4 "</D:prop></D:propstat></D:response>"
#define XML5 "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktoken><D:href>"
#define XML6 "</D:href></D:locktoken></D:activelock></D:lockdiscovery></D:prop>"

static char pathName[IN_FILE_NAME_LEN];
static httpd_req_t* req;
static char formattedTime[80];
static const char* extensions[] = {"dummy", ".htm", ".css", ".txt", ".js", ".json", ".png", ".gif", ".jpg", ".ico", ".svg", ".xml", ".pdf", ".zip", ".gz"};
static const char* mimeTypes[] = {"application/octet-stream", "text/html", "text/html", "text/css", "text/plain", "application/javascript", "application/json", "image/png", "image/gif", "image/jpeg", "image/x-icon", "image/svg+xml", "text/xml", "application/pdf", "application/zip", "application/x-gzip"};

static int getMimeType(const char* path) {
  // determine mime type for give file extension
  int mimePtr = 1;
  size_t len = strlen(path);
  for (const char* ext : extensions) {
    size_t slen = strlen(ext);
    if (!strncmp(path + len - slen, ext, slen)) return mimePtr;
    mimePtr++;
  }
  return 0; // default mime type
}
  
static void formatTime(time_t t) {
  // format time for XML property values
  tm* timeinfo = gmtime(&t);
  strftime(formattedTime, sizeof(formattedTime), "%a, %d %b %Y %H:%M:%S %Z", timeinfo);
}

static bool haveResource(bool ignore = false) {
  // check if file or folder exists
  if (STORAGE.exists(pathName)) return true;
  else if (!ignore) httpd_resp_send_404(req); 
  return false;
} 

static bool isFolder() {
  // identify if resource is file of folder
  File root = STORAGE.open(pathName);
  bool res = root.isDirectory();
  root.close();
  return res;
}

static void sendContentProp(const char* prop, const char* value) {
  // set individual XML properties in response
  char propStr[strlen(prop) * 2 + strlen(value) + 15];
  sprintf(propStr, "<D:%s>%s</D:%s>", prop, value, prop);
  httpd_resp_sendstr_chunk(req, propStr);
  LOG_VRB("propStr %s", propStr);
}

static void sendPropResponse(File& file, const char* payload) {
  // send SD properties details to PC
  size_t encodeLen = 3 + strlen(file.path()) * 2;
  size_t maxLen = strlen(XML2) + encodeLen + strlen(XML3);
  char resp[maxLen + 1];
  snprintf(resp, maxLen, "%s%s%s", XML2, file.path(), XML3);
  httpd_resp_sendstr_chunk(req, resp);
  LOG_VRB("resp xml: %s", resp);
  
  formatTime(file.getLastWrite());
  sendContentProp("getlastmodified", formattedTime);
  sendContentProp("creationdate", formattedTime);

  if (file.isDirectory()) sendContentProp("resourcetype", "<D:collection/>");
  else {
    char fsizeStr[15];
    sprintf(fsizeStr, "%u", file.size());
    sendContentProp("getcontentlength", fsizeStr);
    sendContentProp("getcontenttype", mimeTypes[getMimeType(file.path())]);
    httpd_resp_sendstr_chunk(req, "<resourcetype/>");
  }
  sendContentProp("displayname", file.name());
  
  if (strlen(payload)) {
    // return quota data if requested
    if (strstr(payload, "quota-available-bytes") != NULL || strstr(payload, "quota-used-bytes") != NULL) {
      char numberStr[15];
      sprintf(numberStr, "%llu", (uint64_t)STORAGE.totalBytes() - (uint64_t)STORAGE.usedBytes());
      sendContentProp("quota-available-bytes", numberStr);
      sprintf(numberStr, "%llu", (uint64_t)STORAGE.usedBytes());
      sendContentProp("quota-used-bytes", numberStr);
    }
  }
  httpd_resp_sendstr_chunk(req, XML4);
}

static bool getPayload(char* payload) {
  // get payload in PROPFIND message
  int bytesRead = -1;
  size_t offset = 0;
  size_t psize = req->content_len;
  if (psize) {
    do {
      bytesRead = httpd_req_recv(req, payload + offset, psize - offset);
      if (bytesRead < 0) {  
        if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
          delay(10);
          continue;
        } else {
          LOG_WRN("Transfer request failed with status %i", bytesRead);
          psize = 0;
          break;
        }
      } else offset += bytesRead;
    } while (bytesRead > 0);
    payload[psize] = 0;  
    LOG_VRB("payload: %s\n", payload);
  }
  return bytesRead < 0 ? false : true;
}

static bool handleProp() {
  // provide details of SD content to PC
  if (!haveResource()) return false;
  // get depth header
  bool depth = false;
  char value[10];
  if (extractHeaderVal(req, "Depth", value) == ESP_OK) depth = (!strcmp(value, "0")) ? false : true;

  // get request payload content if present
  char payload[req->content_len + 1] = {0};
  if (req->content_len) getPayload(payload); 
  
  // common header
  httpd_resp_set_status(req, "207 Multi-Status");
  httpd_resp_set_type(req, "application/xml;charset=utf-8");
  httpd_resp_sendstr_chunk(req, XML1);
  
  // return details of selected folder
  File root = STORAGE.open(pathName);
  sendPropResponse(root, payload);
  if (depth && root.isDirectory()) {
    // if requested return details of each resource in folder
    File entry = root.openNextFile();
    while (entry) {
      sendPropResponse(entry, "");
      entry.close();
      entry = root.openNextFile();
    }
  }
  root.close();
  httpd_resp_sendstr_chunk(req, "</D:multistatus>");
  httpd_resp_sendstr_chunk(req, NULL);
  return true;
}

static bool handleOptions() {
  httpd_resp_sendstr(req, NULL); 
  return true;
}

static bool handleGet() {
  // transfer file to PC
  if (!haveResource()) return false;
  if (isFolder()) {
    httpd_resp_send_404(req);
    return false;
  } else {
    httpd_resp_set_type(req, mimeTypes[getMimeType(pathName)]);
    strcpy(inFileName, pathName);
    esp_err_t res = fileHandler(req); // file content
    return res == ESP_OK ? true : false;
  }
  return true;
}

static bool handleHead() {
  if (!haveResource()) return false;
  httpd_resp_sendstr(req, NULL);
  return true;
}

static bool handleLock() {
  // provide (dummy) lock while file open
  const char* lockToken = "0123456789012345";
  httpd_resp_set_hdr(req, "Lock-Token", lockToken);
  char resp[strlen(XML5) + strlen(lockToken) + strlen(XML6) + 1];
  sprintf(resp, "%s%s%s", XML5, lockToken, XML6);
  httpd_resp_set_type(req, "application/xml;charset=utf-8");
  httpd_resp_sendstr(req, resp);
  return true;
} 

static bool handleUnlock() {
  // unlock file when closed
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_sendstr(req, NULL);
  return true;
}

static bool handlePut() {
  // transfer file from PC
  if (isFolder()) return false;
  if (!haveResource(true) || !req->content_len) {
    // if no content, create file entry only 
    File file = STORAGE.open(pathName, FILE_WRITE);
    file.close();
    httpd_resp_set_status(req, "201 Created");
    httpd_resp_sendstr(req, NULL);
  }
  if (req->content_len) {
    // transfer file content to SD
    strcpy(inFileName, pathName);
    esp_err_t res = uploadHandler(req);
    return res == ESP_OK ? true : false;
  } 
  return true;
}

static bool handleDelete() {
  // delete file or folder
  if (!haveResource()) return false;
  // for this app, single folder level only
  deleteFolderOrFile(pathName);
  httpd_resp_sendstr(req, NULL);
  return true;
}

static bool handleMkdir() {
  // create new folder
  if (haveResource(true)) return false; // already exists
  bool res = STORAGE.mkdir(pathName);
  if (res) httpd_resp_set_status(req, "201 Created");
  else httpd_resp_set_status(req, "500 Internal Server Error");
  httpd_resp_sendstr(req, NULL);
  return res; 
}

static bool checkSamePath(const char *source_path, const char *dest_path) {
  // Compare paths, excluding filenames 
  char source_dir[strlen(source_path) + 1];
  char dest_dir[strlen(dest_path) + 1];
  strncpy(source_dir, source_path, strrchr(source_path, '/') - source_path);
  source_dir[strrchr(source_path, '/') - source_path] = 0;
  strncpy(dest_dir, dest_path, strrchr(dest_path, '/') - dest_path);
  dest_dir[strrchr(dest_path, '/') - dest_path] = 0;
  return strcmp(source_dir, dest_dir) == 0;
}

static bool handleMove() {
  // rename file or folder, or change file location
  bool res = false;
  char dest[100];
  if (extractHeaderVal(req, "Destination", dest) == ESP_OK) {
    // obtain destination filename
    res = true;
    urlDecode(dest);
    char* pos = strstr(dest, WEBDAV);
    memmove(dest, pos + strlen(WEBDAV), strlen(dest));
  
    // only allow renaming if a folder
    if (isFolder()) res = checkSamePath(pathName, dest);
    if (res) {
      res = STORAGE.rename(pathName, dest);
      if (res) httpd_resp_set_status(req, "201 Created");
      else httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, NULL);
      return true;
    } 
  } 
  httpd_resp_send_404(req);
  return false;
}

static bool handleCopy() {
  // copy folder - not implemented
  // files can be copied by copy / paste actions
  httpd_resp_send_404(req);
  return false;
}

bool handleWebDav(httpd_req_t* rreq) {
  // extract method to determine which WebDAV action to take
  //showHttpHeaders(rreq);
  req = rreq;
  sprintf(pathName, "%s", req->uri + strlen(WEBDAV)); // strip out "/webdav"
  if (pathName[strlen(pathName) - 1] == '/') pathName[strlen(pathName) - 1] = 0; // remove final / if present
  if (!strlen(pathName)) strcpy(pathName, "/"); // if pathname empty, use single /
  urlDecode(pathName);
  // common response header
  httpd_resp_set_hdr(req, "DAV", "1");
  httpd_resp_set_hdr(req, "Allow", ALLOW);

  switch(req->method) {
    case HTTP_PUT: return handlePut(); // file create/uploads
    case HTTP_PROPFIND: return handleProp(); // get file or directory properties
    case HTTP_PROPPATCH: return handleProp(); // set file or directory properties
    case HTTP_GET: return handleGet(); // file downloads
    case HTTP_HEAD: return handleHead(); // file properties
    case HTTP_OPTIONS: return handleOptions(); // supported options
    case HTTP_LOCK: return handleLock(); // open file lock
    case HTTP_UNLOCK: return handleUnlock(); // close file lock
    case HTTP_MKCOL: return handleMkdir(); // folder creation
    case HTTP_MOVE: return handleMove(); // rename or move file or directory 
    case HTTP_DELETE: return handleDelete(); // delete a file or directory
    case HTTP_COPY: return handleCopy(); // copy a file or directory
    default: {
      LOG_ERR("Unhandled method %s", HTTP_METHOD_STRING(req->method));
      httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unhandled method");
      return false;
    }
  }
  return true;
}

#endif
