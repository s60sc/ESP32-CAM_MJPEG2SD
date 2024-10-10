
// Simple SMTP client for sending email message with attachment
//
// Only tested with Gmail sender account 
//
// Prereqs for Gmail sender account:
// - recommended to create a dedicated email account
// - create an app password - https://support.google.com/accounts/answer/185833
// - set smtpUse in web config page to true, and enter account details on web page
//
// s60sc 2022 

#include "appGlobals.h"

#if INCLUDE_SMTP
#if (!INCLUDE_CERTS)
const char* smtp_rootCACertificate = "";
#endif

// SMTP connection params, setup via web page
char smtp_login[MAX_HOST_LEN]; // sender email account 
char SMTP_Pass[MAX_PWD_LEN]; // 16 digit app password, not account password
char smtp_email[MAX_HOST_LEN]; // receiver, can be same as smtp_login, or be any other email account
char smtp_server[MAX_HOST_LEN]; // the email service provider, eg smtp.gmail.com"
uint16_t smtp_port; // gmail SSL port 465; 

#define MIME_TYPE "image/jpg"
#define ATTACH_NAME "frame.jpg"

// SMTP control
// Calling function has to populate SMTPbuffer and set smtpBufferSize for attachment data
TaskHandle_t emailHandle = NULL; 
static char rspBuf[256]; // smtp response buffer
static char respCodeRx[4]; // smtp response code 
static char subject[50];
static char message[100];

bool smtpUse = false; // whether or not to send email alerts
int emailCount = 0;
int alertMax = 10; // only applied to emails

static bool sendSmtpCommand(NetworkClientSecure& client, const char* cmd, const char* respCode) {
  // wait from smtp server response, check response code and extract response data
  LOG_VRB("Cmd: %s", cmd);
  if (strlen(cmd)) client.println(cmd);
  
	uint32_t start = millis();
  while (!client.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!client.available()) {
    LOG_WRN("SMTP server response timeout");
    return false;
  }

  // read in response code and message
  client.read((uint8_t*)respCodeRx, 3); 
  respCodeRx[3] = 0; // terminator
  int readLen = client.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (client.available()) client.read(); // bin the rest of response

  // check response code with expected
  LOG_VRB("Rx code: %s, resp: %s", respCodeRx, rspBuf);
  if (strcmp(respCodeRx, respCode) != 0) {
    // incorrect response code
    LOG_ERR("Command %s got wrong response: %s", cmd, rspBuf);
    return false;
  }
	return true;
}

static bool emailSend(const char* mimeType = MIME_TYPE, const char* fileName = ATTACH_NAME) {

  // send email to defined smtp server
  char content[100];
  
  NetworkClientSecure client;
  bool res = remoteServerConnect(client, smtp_server, smtp_port, smtp_rootCACertificate, EMAILCONN); 
  if (!res) return false;
  
  while (true) { // fake non loop to enable breaks
    res = false;
    if (!sendSmtpCommand(client, "", "220")) break;
  
    sprintf(content, "HELO %s: ", APP_NAME);
    if (!sendSmtpCommand(client, content, "250")) break;
    
    if (!sendSmtpCommand(client, "AUTH LOGIN", "334")) break; 
    if (!sendSmtpCommand(client, encode64(smtp_login), "334")) break;
    if (!sendSmtpCommand(client, encode64(SMTP_Pass), "235")) break;
  
    // send email header
    sprintf(content, "MAIL FROM: <%s>", APP_NAME);
    if (!sendSmtpCommand(client, content, "250")) break;
    sprintf(content, "RCPT TO: <%s>", smtp_email);
    if (!sendSmtpCommand(client, content, "250")) break;
  
    // send message body header
    if (!sendSmtpCommand(client, "DATA", "354")) break;
    sprintf(content, "From: \"%s\" <%s>", APP_NAME, smtp_login);
    client.println(content);
    sprintf(content, "To: <%s>", smtp_email);
    client.println(content);
    sprintf(content, "Subject: %s", subject);
    client.println(content);
  
    // send message
    client.println("MIME-Version: 1.0");
    sprintf(content, "Content-Type: Multipart/mixed; boundary=%s", BOUNDARY_VAL);
    client.println(content);
    sprintf(content, "--%s", BOUNDARY_VAL);
    client.println(content);
    client.println("Content-Type: text/plain; charset=UTF-8");
    client.println("Content-Transfer-Encoding: quoted-printable");
    client.println("Content-Disposition: inline");
    client.println();
    client.println(message);
    client.println();
    
    if (alertBufferSize) {
      // send attachment
      client.println(content); // boundary
      sprintf(content, "Content-Type: %s", mimeType); 
      client.println(content);
      client.println("Content-Transfer-Encoding: base64");
      sprintf(content, "Content-Disposition: attachment; filename=\"%s\"", fileName); 
      client.println(content); 
      // base64 encode attachment and send out in chunks
      size_t chunkSize = 3;
      for (size_t i = 0; i < alertBufferSize; i += chunkSize) 
        client.write(encode64chunk(alertBuffer + i, min(alertBufferSize - i, chunkSize)), 4);
    } 
    client.println("\n"); // two lines to finish header
        
    // close message data and quit
    if (!sendSmtpCommand(client, ".", "250")) break;
    if (!sendSmtpCommand(client, "QUIT", "221")) break;
    res = true;
    break;
  }
  // cleanly terminate connection
  remoteServerClose(client);
  alertBufferSize = 0;
  return res;
}

static void emailTask(void* parameter) {
  //  send email
  if (emailCount < alertMax) { 
    // send email if under daily limit
    if (emailSend()) LOG_ALT("Sent daily email %u", emailCount + 1);
    else LOG_WRN("Failed to send email");
  }
  if (++emailCount >= alertMax) LOG_WRN("Daily email limit %u reached", alertMax);
  emailHandle = NULL;
  vTaskDelete(NULL);
}

void emailAlert(const char* _subject, const char* _message) {
  // send email to alert on required event
  if (smtpUse) {
    if (alertBuffer != NULL) {
      if (emailHandle == NULL) {
        strncpy(subject, _subject, sizeof(subject)-1);
        snprintf(subject+strlen(subject), sizeof(subject)-strlen(subject), " from %s", hostName);
        strncpy(message, _message, sizeof(message)-1);
        xTaskCreate(&emailTask, "emailTask", EMAIL_STACK_SIZE, NULL, EMAIL_PRI, &emailHandle);
        debugMemory("emailAlert");
      } else LOG_WRN("Email alert already in progress");
    } else LOG_WRN("Need to restart to setup email");
  }
}

void prepSMTP() {
  if (smtpUse) {
    emailCount = 0;
    if (alertBuffer == NULL) alertBuffer = (byte*)ps_malloc(MAX_JPEG); 
    LOG_INF("Email alerts active");
  } 
}

#endif
