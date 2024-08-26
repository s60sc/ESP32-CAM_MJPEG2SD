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

 Define app to have static IP address, and use as variable substitution for openssl:
   set APP_IP="192.168.1.135"
 Create app server private key and public certificate:
   openssl req -nodes -x509 -sha256 -newkey rsa:4096 -subj "/CN=%APP_IP%" -addext "subjectAltName = IP:%APP_IP%" -extensions v3_ca -keyout prvtkey.pem -out cacert.pem -days 800
 
 Paste content of prvtkey.pem and cacert.pem files into prvtkey_pem and cacert_pem constants below.
 View server cert content:
   openssl x509 -in cacert.pem -noout -text

 Use of HTTPS is controlled on web page by option 'Use HTTPS' under Access Settings / Authentication settings 
 If the private key or public certificate constants are empty, the Use HTTPS setting is ignored.
 
 Enter `https://static_ip` to access the app from the browser. A security warning will be displayed as the certificate is self signed so untrusted. To trust the certificate it needs to be installed on the device: 
 - open the Chrome settings page.
 - in the Privacy and security panel, expand the Security section, click on Manage certificates.
 - in the Certificates popup, select the Trusted Root Certification Authorities tab, click the Import... button to launch the Import Wizard.
 - click Next, on the next page, select Browse... and locate the cacert.pem file.
 - click Next, then Finish,then in the Security Warning popup, click on Yes and another popup indicates that the import was successful.

 s60sc 2023
 */

#include "appGlobals.h"

#if INCLUDE_CERTS

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

// Paste in app server private key
const char* prvtkey_pem = R"~(
-----BEGIN PRIVATE KEY-----
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
B5e2Vlm3qrdvy8qCTHRrjxiMEceq
-----END PRIVATE KEY-----
)~";

// Paste in self signed app server public certificate
const char* cacert_pem = R"~(
-----BEGIN CERTIFICATE-----
MIIFIjCCAwqgAwIBAgIUM5ivBIoTo1Mdi/HXg0OSH0S8ww8wDQYJKoZIhvcNAQEL
BQAwGDEWMBQGA1UEAwwNMTkyLjE2OC4xLjEzNTAeFw0yMzEyMjIxNzU0MjdaFw0y
NjAzMDExNzU0MjdaMBgxFjAUBgNVBAMMDTE5Mi4xNjguMS4xMzUwggIiMA0GCSqG
SIb3DQEBAQUAA4ICDwAwggIKAoICAQDy/QRi/nRv+I1qwoddDXtvGHMkMA8Tznvg
tVBkh4FBaPQl/0v455/hUVKbyHq9MyH+S9mel0nVuJIQKZPfF/hSttt1C5963Hp8
c6SvoGBfS6iIVp7DKBupiMXYKn+4k1SK7v9LAPRcGYACZ2juuthhixD+Z1ylsOvI
Fu5il4cdm7e3HLL0eM5cw52+I+MrcjwVhP37IEdEflhpjZfgZb8PAhSiK/gj+N4U
eVYSsegvXk0fss4MAenttJ6g9XJJolqfMGI+abYsdg9jgKfaMZJCqx+RpD7VrsQv
v3NTeXL4v7u4+H8FGVbkvpiVP8QKfk2ptiDQ3FC2yvyZyKgVTHQXo1htxR8dMQW2
eHoUMCxXEP5tHNBiQoHVudL0eItqHS3Zsv6dWMDpEZb3VShnaGxNqEpiz/cnTOZy
TtLqItofnmsOOu/GeAwV7FfnKgDhuvBxe+dAuJWs4lk42hgMNvpHm811Gw51ucYC
+2Kshw1VQmhYQGYnR2ynNf99kcc3NCqsdU3xwOIliilyxbagcyDa11y7L8UIpYmC
HTzBzUEdyAFNBPnyiSMUVXdG7FTGnyDbkz10zdmFBTysCEsB5tgt5Ul46XXAUkTc
314dqNi4qMz4oBhv2lBnJLbcn7EgqXJJYh13JCmJcT31vMD232wVMWNPs0NwFKpT
27cAtIBFTQIDAQABo2QwYjAdBgNVHQ4EFgQULbmd0u6MQvmz8NjD5kSew1jKXg0w
HwYDVR0jBBgwFoAULbmd0u6MQvmz8NjD5kSew1jKXg0wDwYDVR0TAQH/BAUwAwEB
/zAPBgNVHREECDAGhwTAqAGHMA0GCSqGSIb3DQEBCwUAA4ICAQByghxxDQ9AGlK0
t2+HKUnd/+rTn1YsD7uNNYaKK0Nmm9O6Bq0/cARsD0YGwpBGVloWoWoWKIuvJA+9
p2UmKGAlTWz0+JaVbDEpi1XegIi2ZR8CQNnngpy7lBzCwiKxils/kwTv7Hzakia7
Ddbd+0qxJcA5MUg45jCamqY/jNChdNe9TPupfWJ9E+6E6d5aIlo50zXBfDlfES+Z
YS5TL6wxomCaWI33a/I+pZE5wtAy+bGzznSkF8Sx4kn3I6ab60rjG+prqiqHwTt2
00JZJhe6bQc+shPe7qmuNJeW/uFwPAdE1df6h5A6biSLUCenfZP+7FgL9tl0baMn
LjpOB9PTJ5sK1S/GrnwmdKXOiluY7Mqd+vumUluOSGaZdDSrnhop+juI4C603QSs
dBjNKqJJ48QYRTW4qlW9QARcuBq/aX0qLiLTE/rpUaqqhi4qPADb/GVw9e7Iay8r
0nCPHSAony2uTcDEVYxTp/WSL7fxTCXEvwHkJNAZf3qR0NESwbqYGKYdpxDFEDa5
aZm1Jd72d2IvFvXUUPq6FFWKu55qf16QMV76+Ls/19idTrVD1JHf0sd0NS1+Bzwt
R5IfapcWlNOtjpA5AF9AGSor9+rekXtgK1NmXyT9g1zYk/gHlEQNoDAjHP76p7ei
G8kPCt8uxFlnuaH9HPstmlY3qRj9BA==
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
