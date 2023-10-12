 /*
 WiFiClientSecure encrypts connections to remote servers (eg github, smtp)
 To optionally validate identity of remote server (prevent man-in-middle threats), 
 its public certificate needs to be checked by the app.
 Use openssl tool to obtain public certificate of remote server, eg:
   openssl s_client -showcerts -verify 5 -connect raw.githubusercontent.com:443
   openssl s_client -showcerts -verify 5 -connect smtp.gmail.com:465
 Copy and paste last listed certificate (usually root CA certificate) into relevant constant below.
 To disable certificate checking (WiFiClientSecure) leave relevant constant empty, and / or
 on web page under Access Settings / Authentication settings set Use Secure to off

 FTP connection is plaintext as FTPS not implemented.


 To set app as HTTPS server, a server private key and public certificate are required
 Create keys and certificates using openssl tool in WSL

 Define app to have static IP address, and use as variable substitution for openssl:
   APP_IP="192.168.1.133"
 Create app server private key and public certificate:
   openssl req -nodes -x509 -sha256 -newkey rsa:4096 -subj "/CN='${APP_IP}'" -extensions v3_ca -keyout prvtkey.pem -out cacert.pem -days 800
 
 Paste content of prvtkey.pem and cacert.pem files into prvtkey_pem and cacert_pem constants below.
 View server cert content:
   openssl x509 -in cacert.pem -noout -text

 Use of HTTPS is controlled on web page by option 'Use HTTPS' under Access Settings / Authentication settings 
 If the private key or public certificate constants are empty, the Use HTTPS setting is ignored.

 s60sc 2023
 */

#include "appGlobals.h"

// GitHub public certificate valid till April 2031
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


// Paste in app server private key
const char* prvtkey_pem = R"~(
-----BEGIN PRIVATE KEY-----
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
Swf/z8Yn3GNDO93P/ok0J7ftfS5sGA==
-----END PRIVATE KEY-----
)~";

// Paste in self signed app server public certificate
const char* cacert_pem = R"~(
-----BEGIN CERTIFICATE-----
MIIE+zCCAuOgAwIBAgIUCWwzJVjwg6RLmYg0WeRLXfsJsGIwDQYJKoZIhvcNAQEL
BQAwDTELMAkGA1UEAwwCJycwHhcNMjMwOTEzMTc1NDIyWhcNMjUxMTIxMTc1NDIy
WjANMQswCQYDVQQDDAInJzCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIB
ANqm7vPLQqdj0agJmNYxmtXNUuC33IQH0hWOmA3nDFnlUXjqBjQlkok78vY2KTXp
2E+daqzFoZ04bDnpRWNExUw9RVybNtkrHbzSVfEgo5wdQIz8wzDGoVWurMHhsMcu
oQxhMm7VfamXeZt257iSyTQOc6NXBdbpUJ5BcCCdZjTGw537ISGr026V8EaaA9qE
2BKNUWs9VD2p764cep8ELMipEoY+RUaearyeommkjw/28jDJCktJLTwA4S5n3/zj
uylIdqlRrHIF13ISGwT+jUylQIC3CN+Yerq7knBtCqSmuJ7z1VMWfBFCpwKSjfGV
ONNaggD26AhKJTh+NbLcxrqjOg5GwoRGziXsEN66bJKbDXjID9WdGHGED4b6Bn6h
fVDaMrswTBawBbH2YIndlBWwZYAXVRi/VKMRkOTD5gdWLREbZ8IzNCqKMSAciij2
cC5YqjLfqTRzLUcrQGSk7J3Cqev67Vv7SSeiXY9BnCktiNbyxvO8VHZW3E40Rfqe
220oUWWxRg9aFTPGW1UReZUMcASxueavvRC+tjuX87or5YfwZvjQL8uBwztwN3v+
YvT+FUuPQZH2NYnR/162GvLp8wFn4k6XIqT7cAVWJT7S4zzRGGi3BUDRTnId/e5n
ne6yexpLb+C1dXvzNlrs58y9JW54jyxvoyExYWlEtMC1AgMBAAGjUzBRMB0GA1Ud
DgQWBBSOdJWP3zodWaCgcFPXn0UXlWZnNDAfBgNVHSMEGDAWgBSOdJWP3zodWaCg
cFPXn0UXlWZnNDAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4ICAQDO
feEzqM29OL+Gf67MUqB6IsDI9gAf+rjq+yYd1K38QriG3oKDCR4g0dfaRvQ9lAYb
38zk0JxFK8UjtsZl/+7XlqOv97WZ6rfwz7LkFo9hxAHHeKpHchFFZrdzJQZ0CN0P
rn8s9baZEuX0buKu4ybUpZ5q2D/t9LgDCkiaCa1UbstONk2iKN2UUxdcxDYQf7CD
Pwkawr/eijPp6RBCAUxEvRoRkhZA0JSwq6n7T7iJ1ATXB6b7RoSFRFk9VCEy39Rt
QfIxazNe1dfw2Le3IuY2wRstfNA/49kIyfhsvGIPPsqG9JmaBk4k4hIlxuJyeUc8
iNNyU4Z3P1v+0fP0ApXiuz6UZPfqIVNwUBQG0R0w4eZOkjzpU7nO4gojcnmDGGbR
1xSId9mbHR6naCYN+IQSZXb49M+N/5TPwPItunS9S0mnc7Zb82GylFa3LnLoVvhf
2BNNf1IeRs4Acdupeqaz/7jlVNBZpNk8BujTyj/UzkdWsynw9UMIlh1BMS5X8Hwc
yBVkJHIrBsZCZEN6u8ON7pust2WYRO7nkTKG8cybrHyigpSlh/ezMvO/SN9Pf3j4
JDIBN+Wz4cZTkcvtKxasN1H5ef3AwkkWmCuhD/dzZvjTGnsoTT+mhruZl1AjU/vV
8wguyjwX5Jpo5nvAsL4jLadhDeVxwidBC01WLumGBA==
-----END CERTIFICATE-----
)~";


// Your FTPS Server's root public certificate 
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
