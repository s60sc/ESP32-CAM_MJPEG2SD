 /*
 NetworkClientSecure encrypts connections to remote servers (eg github, smtp)
 To optionally validate identity of remote server (prevent man-in-middle threats), 
 its public certificate needs to be checked by the app.
 Use openssl tool to obtain public certificate of remote server, eg:
   openssl s_client -showcerts -verify 5 -connect raw.githubusercontent.com:443
   openssl s_client -showcerts -verify 5 -connect smtp.gmail.com:465
 Copy and paste last listed certificate (usually root CA certificate) into relevant constant below.
 To disable certificate checking (NetworkClientSecure) leave relevant constant empty, and / or
 on web page under Access Settings / Authentication settings set Use Secure to off

 FTP connection is plaintext as FTPS not implemented.


 To set app as HTTPS server, a server private key and public certificate are required
 Create keys and certificates using openssl tool
 On Windows, paste commands below into a Command Prompt (cmd) window

 Define app to have static IP address, and use this as variable substitution for openssl:
   set APP_IP="192.168.1.135"

 Create app server private key and public certificate:
   openssl req -nodes -x509 -sha256 -newkey rsa:2048 -subj "/CN=%APP_IP%" -addext "subjectAltName=IP:%APP_IP%" -extensions v3_ca -keyout prvtkey.pem -out servercert.pem -days 3660

 View server cert content:
   openssl x509 -in servercert.pem -noout -text

 Use app web page OTA Upload tab to copy servercert.pem and prvtkey.pem into ESP storage.

 Use of HTTPS is controlled on web page by option 'Use HTTPS' under Access Settings / Authentication settings 
 If the private key or public certificate is not loaded, the Use HTTPS setting is ignored.
 
 Enter `https://static_ip` to access the app from the browser. A security warning will be displayed as the certificate is self signed so untrusted. 
 To trust the certificate it needs to be installed on the device: 
 - open the Chrome settings page.
 - in the Privacy and security panel, expand the Security section, click on Manage certificates.
 - in the Certificate Manager panel, press Manage imported certificates from Windows
 - in the Certificates popup, select the Trusted Root Certification Authorities tab, click the Import... button to launch the Import Wizard.
 - click Next, on the next page, select Browse... All Files and locate the servercert.pem file.
 - click Next, then Finish, then in the Security Warning popup, click on Yes and another popup indicates that the import was successful.

 s60sc 2023, 2025
 */

#include "appGlobals.h"

#if INCLUDE_CERTS

#ifndef CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC
#define CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC 1
#endif

#define PRVTKEY DATA_DIR "/prvtkey" ".pem"
#define SERVERCERT DATA_DIR "/servercert" ".pem"

static fs::FS fp = STORAGE;
char* serverCerts[2]; // private key, server key
#define NUM_CERTS 2

void loadCerts() {
  if (useHttps) {
    const char* certFiles[NUM_CERTS] = {PRVTKEY, SERVERCERT};
    for (int i = 0; i < NUM_CERTS; i++) {
      File file;
      if (fp.exists(certFiles[i])) {
        file = fp.open(certFiles[i], FILE_READ);
        if (!file || !file.size()) {
          LOG_WRN("Failed to load file %s", certFiles[i]);
          useHttps = false;
        } else {
          // load contents
          serverCerts[i] = psramFound() ? (char*)ps_malloc(file.size() + 1) : (char*)malloc(file.size() + 1); 
          size_t inBytes = file.readBytes(serverCerts[i], file.size());
          if (inBytes != file.size()) {
            LOG_WRN("File %s not correctly loaded", certFiles[i]);
            useHttps = false;
          }
        }
        file.close();
      } else {
        LOG_WRN("File %s not found", certFiles[i]);
        useHttps = false;
      }
    }
    if (!useHttps) LOG_WRN("HTTPS not available as server keys not loaded, using HTTP");
  }
}


/********* Remote Server Certificates *********/

// GitHub public certificate valid till April 2031
const char* git_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIEyDCCA7CgAwIBAgIQDPW9BitWAvR6uFAsI8zwZjANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0yMTAzMzAwMDAwMDBaFw0zMTAzMjkyMzU5NTlaMFkxCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxMzAxBgNVBAMTKkRpZ2lDZXJ0IEdsb2Jh
bCBHMiBUTFMgUlNBIFNIQTI1NiAyMDIwIENBMTCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBAMz3EGJPprtjb+2QUlbFbSd7ehJWivH0+dbn4Y+9lavyYEEV
cNsSAPonCrVXOFt9slGTcZUOakGUWzUb+nv6u8W+JDD+Vu/E832X4xT1FE3LpxDy
FuqrIvAxIhFhaZAmunjZlx/jfWardUSVc8is/+9dCopZQ+GssjoP80j812s3wWPc
3kbW20X+fSP9kOhRBx5Ro1/tSUZUfyyIxfQTnJcVPAPooTncaQwywa8WV0yUR0J8
osicfebUTVSvQpmowQTCd5zWSOTOEeAqgJnwQ3DPP3Zr0UxJqyRewg2C/Uaoq2yT
zGJSQnWS+Jr6Xl6ysGHlHx+5fwmY6D36g39HaaECAwEAAaOCAYIwggF+MBIGA1Ud
EwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFHSFgMBmx9833s+9KTeqAx2+7c0XMB8G
A1UdIwQYMBaAFE4iVCAYlebjbuYP+vq5Eu0GF485MA4GA1UdDwEB/wQEAwIBhjAd
BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwdgYIKwYBBQUHAQEEajBoMCQG
CCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQAYIKwYBBQUHMAKG
NGh0dHA6Ly9jYWNlcnRzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RH
Mi5jcnQwQgYDVR0fBDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQuY29t
L0RpZ2lDZXJ0R2xvYmFsUm9vdEcyLmNybDA9BgNVHSAENjA0MAsGCWCGSAGG/WwC
ATAHBgVngQwBATAIBgZngQwBAgEwCAYGZ4EMAQICMAgGBmeBDAECAzANBgkqhkiG
9w0BAQsFAAOCAQEAkPFwyyiXaZd8dP3A+iZ7U6utzWX9upwGnIrXWkOH7U1MVl+t
wcW1BSAuWdH/SvWgKtiwla3JLko716f2b4gp/DA/JIS7w7d7kwcsr4drdjPtAFVS
slme5LnQ89/nD/7d+MS5EHKBCQRfz5eeLjJ1js+aWNJXMX43AYGyZm0pGrFmCW3R
bpD0ufovARTFXFZkAdl9h6g4U5+LXUZtXMYnhIHUfoyMo5tS58aI7Dd8KvvwVVo4
chDYABPPTHPbqjc1qCmBaZx2vN4Ye5DUys/vZwP9BFohFrH/6j/f3IL16/RZkiMN
JCqVJUzKoZHm1Lesh3Sz8W2jmdv51b2EQJ8HmA==
-----END CERTIFICATE-----
)~";

// Your FTPS Server's root public certificate (not implemented)
const char* ftps_rootCACertificate = R"~(
)~";


// Your SMTP Server's public certificate (eg smtp.gmail.com valid till Jan 2028)
const char* smtp_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIFYjCCBEqgAwIBAgIQd70NbNs2+RrqIQ/E8FjTDTANBgkqhkiG9w0BAQsFADBX
MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE
CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIwMDYx
OTAwMDA0MloXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT
GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFIx
MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAthECix7joXebO9y/lD63
ladAPKH9gvl9MgaCcfb2jH/76Nu8ai6Xl6OMS/kr9rH5zoQdsfnFl97vufKj6bwS
iV6nqlKr+CMny6SxnGPb15l+8Ape62im9MZaRw1NEDPjTrETo8gYbEvs/AmQ351k
KSUjB6G00j0uYODP0gmHu81I8E3CwnqIiru6z1kZ1q+PsAewnjHxgsHA3y6mbWwZ
DrXYfiYaRQM9sHmklCitD38m5agI/pboPGiUU+6DOogrFZYJsuB6jC511pzrp1Zk
j5ZPaK49l8KEj8C8QMALXL32h7M1bKwYUH+E4EzNktMg6TO8UpmvMrUpsyUqtEj5
cuHKZPfmghCN6J3Cioj6OGaK/GP5Afl4/Xtcd/p2h/rs37EOeZVXtL0m79YB0esW
CruOC7XFxYpVq9Os6pFLKcwZpDIlTirxZUTQAs6qzkm06p98g7BAe+dDq6dso499
iYH6TKX/1Y7DzkvgtdizjkXPdsDtQCv9Uw+wp9U7DbGKogPeMa3Md+pvez7W35Ei
Eua++tgy/BBjFFFy3l3WFpO9KWgz7zpm7AeKJt8T11dleCfeXkkUAKIAf5qoIbap
sZWwpbkNFhHax2xIPEDgfg1azVY80ZcFuctL7TlLnMQ/0lUTbiSw1nH69MG6zO0b
9f6BQdgAmD06yK56mDcYBZUCAwEAAaOCATgwggE0MA4GA1UdDwEB/wQEAwIBhjAP
BgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBTkrysmcRorSCeFL1JmLO/wiRNxPjAf
BgNVHSMEGDAWgBRge2YaRQ2XyolQL30EzTSo//z9SzBgBggrBgEFBQcBAQRUMFIw
JQYIKwYBBQUHMAGGGWh0dHA6Ly9vY3NwLnBraS5nb29nL2dzcjEwKQYIKwYBBQUH
MAKGHWh0dHA6Ly9wa2kuZ29vZy9nc3IxL2dzcjEuY3J0MDIGA1UdHwQrMCkwJ6Al
oCOGIWh0dHA6Ly9jcmwucGtpLmdvb2cvZ3NyMS9nc3IxLmNybDA7BgNVHSAENDAy
MAgGBmeBDAECATAIBgZngQwBAgIwDQYLKwYBBAHWeQIFAwIwDQYLKwYBBAHWeQIF
AwMwDQYJKoZIhvcNAQELBQADggEBADSkHrEoo9C0dhemMXoh6dFSPsjbdBZBiLg9
NR3t5P+T4Vxfq7vqfM/b5A3Ri1fyJm9bvhdGaJQ3b2t6yMAYN/olUazsaL+yyEn9
WprKASOshIArAoyZl+tJaox118fessmXn1hIVw41oeQa1v1vg4Fv74zPl6/AhSrw
9U5pCZEt4Wi4wStz6dTZ/CLANx8LZh1J7QJVj2fhMtfTJr9w4z30Z209fOU0iOMy
+qduBmpvvYuR7hZL6Dupszfnw0Skfths18dG9ZKb59UhvmaSGZRVbNQpsg3BZlvi
d0lIKO2d1xozclOzgjXPYovJJIultzkMu34qQb9Sz/yilrbCgj8=
-----END CERTIFICATE-----
)~";


// Your MQTT Server's public certificate 
const char* mqtt_rootCACertificate = R"~(
)~";

// Telegram server certificate for api.telegram.org, valid till May 2031
const char* telegram_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIEfTCCA2WgAwIBAgIDG+cVMA0GCSqGSIb3DQEBCwUAMGMxCzAJBgNVBAYTAlVT
MSEwHwYDVQQKExhUaGUgR28gRGFkZHkgR3JvdXAsIEluYy4xMTAvBgNVBAsTKEdv
IERhZGR5IENsYXNzIDIgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTQwMTAx
MDcwMDAwWhcNMzEwNTMwMDcwMDAwWjCBgzELMAkGA1UEBhMCVVMxEDAOBgNVBAgT
B0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxGjAYBgNVBAoTEUdvRGFkZHku
Y29tLCBJbmMuMTEwLwYDVQQDEyhHbyBEYWRkeSBSb290IENlcnRpZmljYXRlIEF1
dGhvcml0eSAtIEcyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAv3Fi
CPH6WTT3G8kYo/eASVjpIoMTpsUgQwE7hPHmhUmfJ+r2hBtOoLTbcJjHMgGxBT4H
Tu70+k8vWTAi56sZVmvigAf88xZ1gDlRe+X5NbZ0TqmNghPktj+pA4P6or6KFWp/
3gvDthkUBcrqw6gElDtGfDIN8wBmIsiNaW02jBEYt9OyHGC0OPoCjM7T3UYH3go+
6118yHz7sCtTpJJiaVElBWEaRIGMLKlDliPfrDqBmg4pxRyp6V0etp6eMAo5zvGI
gPtLXcwy7IViQyU0AlYnAZG0O3AqP26x6JyIAX2f1PnbU21gnb8s51iruF9G/M7E
GwM8CetJMVxpRrPgRwIDAQABo4IBFzCCARMwDwYDVR0TAQH/BAUwAwEB/zAOBgNV
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFDqahQcQZyi27/a9BUFuIMGU2g/eMB8GA1Ud
IwQYMBaAFNLEsNKR1EwRcbNhyz2h/t2oatTjMDQGCCsGAQUFBwEBBCgwJjAkBggr
BgEFBQcwAYYYaHR0cDovL29jc3AuZ29kYWRkeS5jb20vMDIGA1UdHwQrMCkwJ6Al
oCOGIWh0dHA6Ly9jcmwuZ29kYWRkeS5jb20vZ2Ryb290LmNybDBGBgNVHSAEPzA9
MDsGBFUdIAAwMzAxBggrBgEFBQcCARYlaHR0cHM6Ly9jZXJ0cy5nb2RhZGR5LmNv
bS9yZXBvc2l0b3J5LzANBgkqhkiG9w0BAQsFAAOCAQEAWQtTvZKGEacke+1bMc8d
H2xwxbhuvk679r6XUOEwf7ooXGKUwuN+M/f7QnaF25UcjCJYdQkMiGVnOQoWCcWg
OJekxSOTP7QYpgEGRJHjp2kntFolfzq3Ms3dhP8qOCkzpN1nsoX+oYggHFCJyNwq
9kIDN0zmiN/VryTyscPfzLXs4Jlet0lUIDyUGAzHHFIYSaRt4bNYC8nY7NmuHDKO
KHAN4v6mF56ED71XcLNa6R+ghlO773z/aQvgSMO3kwvIClTErF0UZzdsyqUvMQg3
qm5vjLyb4lddJIGvl5echK1srDdMZvNhkREg5L4wn3qkKQmw4TRfZHcYQFHfjDCm
rw==
-----END CERTIFICATE-----
)~";


// Your HTTPS File Server public certificate 
const char* hfs_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIDKzCCAhOgAwIBAgIQJOOyowYpLEiLnhpjD0HR5DANBgkqhkiG9w0BAQsFADAg
MR4wHAYDVQQDExVSZWJleCBUaW55IFdlYiBTZXJ2ZXIwHhcNMjMxMTI1MTQ1NzU4
WhcNMzMxMTI1MTQ1NzU4WjAgMR4wHAYDVQQDExVSZWJleCBUaW55IFdlYiBTZXJ2
ZXIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDGgUPfvM+SI1OiOozW
MQUtj/E9461SRFIYfce7b0KANrc/0S4qD11tTQvKvoY7HY5gvc0jgWZBGqQ6/KO7
CIrLnXtsldeUXokcd8cvr8Ko704iOboHLLhewZ4egeBgu6+/1dw6REeExHjNIjiO
1InShPIV8yxX1oUo+EztIo5qecVvrousyIL9KBmAAi7Pdw0/yKQaoXzL9Ehkpv7g
Pbabt6k7W+CofXRhaoGlf5ERvb5T/921PXXdCq7mnB9OjGu0almqYIWh1jRjnBQH
e2avg+LD/+1dakIXXzByhbEpECxtQHZ17iB3DvW0ExiMH+0A8bqQIMp3sOgEzf2J
42XpAgMBAAGjYTBfMA4GA1UdDwEB/wQEAwIHgDAdBgNVHQ4EFgQUUmrFj3+h5tsV
ASwGkjFZ9W5FDf8wEwYDVR0lBAwwCgYIKwYBBQUHAwEwGQYDVR0RBBIwEIIJbG9j
YWxob3N0ggNtcG8wDQYJKoZIhvcNAQELBQADggEBAJ+rf1UUwaZAhsHrL2KX0WPm
E9lCcnBFeQHUILSfM7r7fbEuIXa68mZDeMIV9xs4ex45wN4AAZW1l79Okia9kin8
JkqkhZ/rCvqWsbNt3ryOvWCB2a2JEWW6yRA6EgK+STo3T/Z8Sau0ys8woc7y486l
5BhGu7rlXcbXl8hcEORD/ILxxdae7hHi7sXIReyS2kGiYJUwj+1+6mm26TXuRyCV
jqlsBxH8gnwIlupODKZ/7jU/HhiYaKEbrnNxiOiPeWAw/KJJH5lUxt0piOYIXhj4
DuDay+U7jeJKpND7EYheZY/U6c1wqwXt1DHuFnCCzK8jdOGT9aUSqZUeWfNn9cc=
-----END CERTIFICATE-----
)~";

#endif
