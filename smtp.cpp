
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

// control sending emails 
bool smtpUse; // whether or not to send email alerts
int smtpFrame = 5; // which captured frame number to use for email image
int smtpMaxEmails = 10; // too many could cause account suspension

// SMTP connection params, setup via web page
char smtp_login[32]; // sender email account 
char SMTP_Pass[MAX_PWD_LEN]; // 16 digit app password, not account password
char smtp_email[32]; // receiver, can be same as smtp_login, or be any other email account
char smtp_server[32]; // the email service provider, eg smtp.gmail.com"
uint16_t smtp_port; // gmail SSL port 465; 

#define MIME_TYPE "image/jpg"
#define ATTACH_NAME "frame.jpg"

// SMTP control
// Calling function has to populate SMTPbuffer and set smtpBufferSize for attachment data
size_t smtpBufferSize = 0;
byte* SMTPbuffer = NULL; // buffer for smtp frame
TaskHandle_t emailHandle = NULL; 
static const uint32_t dayLen = 24 * 60 * 60 * 1000;
static uint32_t dayStart;
static int emailCount;
static char rspBuf[256]; // smtp response buffer
static char respCodeRx[4]; // smtp response code 
static char subject[50];
static char message[100];


static bool sendSmtpCommand(WiFiClientSecure& client, const char* cmd, const char* respCode) {
  // wait from smtp server response, check response code and extract response data
  LOG_DBG("Cmd: %s", cmd);
  if (strlen(cmd)) client.println(cmd);
  
	uint32_t start = millis();
  while (!client.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!client.available()) {
    LOG_ERR("SMTP server response timeout");
    return false;
  }

  // read in response code and message
  client.read((uint8_t*)respCodeRx, 3); 
  respCodeRx[3] = 0; // terminator
  int readLen = client.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (client.available()) client.read(); // bin the rest of response

  // check response code with expected
  LOG_DBG("Rx code: %s, resp: %s", respCodeRx, rspBuf);
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
  WiFiClientSecure client;
  client.setInsecure(); // not SSL

  // connect to smtp account and authenticate
  if (!client.connect(smtp_server, smtp_port)) {
    // if failure reports 'SSL - Memory allocation failed', 
    // this indicates lack of heap space
    char errBuf[100];
    client.lastError(errBuf, 100);
    checkMemory();
	  LOG_ERR("Could not connect to mail server %s on port %u: %s", 
	    smtp_server, smtp_port, errBuf);
	  return false;
  }
  bool res = false;
  while (true) { // fake non loop to enable breaks
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
    
    if (smtpBufferSize) {
      // send attachment
      client.println(content); // boundary
      sprintf(content, "Content-Type: %s", mimeType); 
      client.println(content);
      client.println("Content-Transfer-Encoding: base64");
      sprintf(content, "Content-Disposition: attachment; filename=\"%s\"", fileName); 
      client.println(content); 
      // base64 encode attachment and send out in chunks
      size_t chunk = 3;
      for (size_t i = 0; i < smtpBufferSize; i += chunk) 
        client.write(encode64chunk(SMTPbuffer + i, min(smtpBufferSize - i, chunk)), 4);
    } 
    client.println("\n"); // two lines to finish header
        
    // close message data and quit
    if (!sendSmtpCommand(client, ".", "250")) break;
    if (!sendSmtpCommand(client, "QUIT", "221")) break;
    res = true;
    break;
  }
  // cleanly terminate connection
  client.flush();
  client.stop();
  smtpBufferSize = 0;
  return res;
}

static void emailTask(void* parameter) {
  //  send email
  if (millis() - dayStart > dayLen) {
    // reset counters when a day has elapsed
    emailCount = 0;
    dayStart = millis();
    LOG_INF("Reset daily email allowance");
  }
  if (emailCount < smtpMaxEmails) { 
    // send email if under daily limit
    if (emailSend()) LOG_ALT("Sent daily email %u", emailCount + 1);
    else LOG_WRN("Failed to send email");
  }
  if (++emailCount == smtpMaxEmails) LOG_WRN("Daily email limit %u reached", smtpMaxEmails);
  emailHandle = NULL;
  vTaskDelete(NULL);
}

void emailAlert(const char* _subject, const char* _message) {
  // send email to alert on required event
  if (smtpUse) {
    if (emailHandle == NULL) {
      strncpy(subject, _subject, 20);
      snprintf(subject+strlen(subject), 30, " from %s", hostName);
      strncpy(message, _message, 100);
      xTaskCreate(&emailTask, "emailTask", 1024 * 6, NULL, 1, &emailHandle);
    } else LOG_WRN("Email alert already in progress");
  }
}

void prepSMTP() {
  if (smtpUse) {
    dayStart = millis();
    emailCount = 0;
    if (SMTPbuffer == NULL) SMTPbuffer = (byte*)ps_malloc(ONEMEG/2); 
    LOG_INF("Email alerts active");
  } 
  debugMemory("prepSmtp");
}
