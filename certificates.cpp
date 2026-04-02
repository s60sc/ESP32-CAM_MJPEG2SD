 /*
 NetworkClientSecure encrypts connections to remote servers (eg github, smtp)
 To optionally validate identity of remote server (prevent man-in-middle threats), 
 its public certificate needs to be checked by the app.
 Use openssl tool to obtain public certificate of remote server, eg:
   openssl s_client -showcerts -verify 5 -connect raw.githubusercontent.com:443
   openssl s_client -showcerts -verify 5 -connect smtp.gmail.com:465
   openssl s_client -showcerts -verify 5 -connect api.telegram.org:443
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

 Use of HTTPS is controlled on web page by option 'Use HTTPS' under Access Settings / Authentication settings or Edit Config / Network settings
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

char* serverCerts[2]; // private key, server key
#define NUM_CERTS 2

void loadCerts() {
  if (useHttps) {
    const char* certFiles[NUM_CERTS] = {PRVTKEY, SERVERCERT};
    for (int i = 0; i < NUM_CERTS; i++) {
      File file;
      if (STORAGE.exists(certFiles[i])) {
        file = STORAGE.open(certFiles[i], FILE_READ);
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

// GitHub public certificate valid till end 2028
const char* git_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIFgTCCBGmgAwIBAgIQOXJEOvkit1HX02wQ3TE1lTANBgkqhkiG9w0BAQwFADB7
MQswCQYDVQQGEwJHQjEbMBkGA1UECAwSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYD
VQQHDAdTYWxmb3JkMRowGAYDVQQKDBFDb21vZG8gQ0EgTGltaXRlZDEhMB8GA1UE
AwwYQUFBIENlcnRpZmljYXRlIFNlcnZpY2VzMB4XDTE5MDMxMjAwMDAwMFoXDTI4
MTIzMTIzNTk1OVowgYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpOZXcgSmVyc2V5
MRQwEgYDVQQHEwtKZXJzZXkgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBO
ZXR3b3JrMS4wLAYDVQQDEyVVU0VSVHJ1c3QgUlNBIENlcnRpZmljYXRpb24gQXV0
aG9yaXR5MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAgBJlFzYOw9sI
s9CsVw127c0n00ytUINh4qogTQktZAnczomfzD2p7PbPwdzx07HWezcoEStH2jnG
vDoZtF+mvX2do2NCtnbyqTsrkfjib9DsFiCQCT7i6HTJGLSR1GJk23+jBvGIGGqQ
Ijy8/hPwhxR79uQfjtTkUcYRZ0YIUcuGFFQ/vDP+fmyc/xadGL1RjjWmp2bIcmfb
IWax1Jt4A8BQOujM8Ny8nkz+rwWWNR9XWrf/zvk9tyy29lTdyOcSOk2uTIq3XJq0
tyA9yn8iNK5+O2hmAUTnAU5GU5szYPeUvlM3kHND8zLDU+/bqv50TmnHa4xgk97E
xwzf4TKuzJM7UXiVZ4vuPVb+DNBpDxsP8yUmazNt925H+nND5X4OpWaxKXwyhGNV
icQNwZNUMBkTrNN9N6frXTpsNVzbQdcS2qlJC9/YgIoJk2KOtWbPJYjNhLixP6Q5
D9kCnusSTJV882sFqV4Wg8y4Z+LoE53MW4LTTLPtW//e5XOsIzstAL81VXQJSdhJ
WBp/kjbmUZIO8yZ9HE0XvMnsQybQv0FfQKlERPSZ51eHnlAfV1SoPv10Yy+xUGUJ
5lhCLkMaTLTwJUdZ+gQek9QmRkpQgbLevni3/GcV4clXhB4PY9bpYrrWX1Uu6lzG
KAgEJTm4Diup8kyXHAc/DVL17e8vgg8CAwEAAaOB8jCB7zAfBgNVHSMEGDAWgBSg
EQojPpbxB+zirynvgqV/0DCktDAdBgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rID
ZsswDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQFMAMBAf8wEQYDVR0gBAowCDAG
BgRVHSAAMEMGA1UdHwQ8MDowOKA2oDSGMmh0dHA6Ly9jcmwuY29tb2RvY2EuY29t
L0FBQUNlcnRpZmljYXRlU2VydmljZXMuY3JsMDQGCCsGAQUFBwEBBCgwJjAkBggr
BgEFBQcwAYYYaHR0cDovL29jc3AuY29tb2RvY2EuY29tMA0GCSqGSIb3DQEBDAUA
A4IBAQAYh1HcdCE9nIrgJ7cz0C7M7PDmy14R3iJvm3WOnnL+5Nb+qh+cli3vA0p+
rvSNb3I8QzvAP+u431yqqcau8vzY7qN7Q/aGNnwU4M309z/+3ri0ivCRlv79Q2R+
/czSAaF9ffgZGclCKxO/WIu6pKJmBHaIkU4MiRTOok3JMrO66BQavHHxW/BBC5gA
CiIDEOUMsfnNkjcZ7Tvx5Dq2+UUTJnWvu6rvP3t3O9LEApE9GQDTF1w52z97GA1F
zZOFli9d31kWTz9RvdVFGD/tSo7oBmF0Ixa1DVBzJ0RHfxBdiSprhTEUxOipakyA
vGp4z7h/jnZymQyd/teRCBaho1+V
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

// Telegram server certificate for api.telegram.org, valid till Jun 2034
const char* telegram_rootCACertificate = R"~(
-----BEGIN CERTIFICATE-----
MIIEADCCAuigAwIBAgIBADANBgkqhkiG9w0BAQUFADBjMQswCQYDVQQGEwJVUzEh
MB8GA1UEChMYVGhlIEdvIERhZGR5IEdyb3VwLCBJbmMuMTEwLwYDVQQLEyhHbyBE
YWRkeSBDbGFzcyAyIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MB4XDTA0MDYyOTE3
MDYyMFoXDTM0MDYyOTE3MDYyMFowYzELMAkGA1UEBhMCVVMxITAfBgNVBAoTGFRo
ZSBHbyBEYWRkeSBHcm91cCwgSW5jLjExMC8GA1UECxMoR28gRGFkZHkgQ2xhc3Mg
MiBDZXJ0aWZpY2F0aW9uIEF1dGhvcml0eTCCASAwDQYJKoZIhvcNAQEBBQADggEN
ADCCAQgCggEBAN6d1+pXGEmhW+vXX0iG6r7d/+TvZxz0ZWizV3GgXne77ZtJ6XCA
PVYYYwhv2vLM0D9/AlQiVBDYsoHUwHU9S3/Hd8M+eKsaA7Ugay9qK7HFiH7Eux6w
wdhFJ2+qN1j3hybX2C32qRe3H3I2TqYXP2WYktsqbl2i/ojgC95/5Y0V4evLOtXi
EqITLdiOr18SPaAIBQi2XKVlOARFmR6jYGB0xUGlcmIbYsUfb18aQr4CUWWoriMY
avx4A6lNf4DD+qta/KFApMoZFv6yyO9ecw3ud72a9nmYvLEHZ6IVDd2gWMZEewo+
YihfukEHU1jPEX44dMX4/7VpkI+EdOqXG68CAQOjgcAwgb0wHQYDVR0OBBYEFNLE
sNKR1EwRcbNhyz2h/t2oatTjMIGNBgNVHSMEgYUwgYKAFNLEsNKR1EwRcbNhyz2h
/t2oatTjoWekZTBjMQswCQYDVQQGEwJVUzEhMB8GA1UEChMYVGhlIEdvIERhZGR5
IEdyb3VwLCBJbmMuMTEwLwYDVQQLEyhHbyBEYWRkeSBDbGFzcyAyIENlcnRpZmlj
YXRpb24gQXV0aG9yaXR5ggEAMAwGA1UdEwQFMAMBAf8wDQYJKoZIhvcNAQEFBQAD
ggEBADJL87LKPpH8EsahB4yOd6AzBhRckB4Y9wimPQoZ+YeAEW5p5JYXMP80kWNy
OO7MHAGjHZQopDH2esRU1/blMVgDoszOYtuURXO1v0XJJLXVggKtI3lpjbi2Tc7P
TMozI+gciKqdi0FuFskg5YmezTvacPd+mSYgFFQlq25zheabIZ0KbIIOqPjCDPoQ
HmyW74cNxA9hi63ugyuV+I6ShHI56yDqg+2DzZduCLzrTia2cyvk0/ZM/iZx4mER
dEr/VxqHD3VILs9RaRegAhJhldXRQLIQTO7ErBBDpqWeCtWVYpoNz4iCxTIM5Cuf
ReYNnyicsbkqWletNw+vHX/bvZ8=
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
