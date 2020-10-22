#include <ArduinoJson.h>  // for parsing JSON files
#include <WiFi.h>         // for connecting to the local WiFi network
#include <WiFiClientSecure.h>   // for accessing NTP real time API data

#include "corona_information.h"

// Open Weather Map API server name
const char server[] = "services7.arcgis.com"; // server name
// Ennepe-Ruhr-Kreis = 29
// Remscheid = 71
String object_id; // city name and country code
const char sslCertificate[] PROGMEM = R"=====(
-----BEGIN CERTIFICATE-----
MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j
ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL
MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3
LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug
RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm
+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW
PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM
xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB
Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3
hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg
EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF
MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA
FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec
nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z
eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF
hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2
Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe
vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep
+OkuE6N36B9K
-----END CERTIFICATE-----
)=====";

WiFiClientSecure client;

uint16_t letzte_inzidenz = 0;
String text; // String for JSON parsing

// macro and variables used for parsing JSON data
#define JSON_BUFF_DIMENSION 2500
int jsonend = 0;
boolean startJson = false;

void parseJson(const char * jsonString);

uint16_t get_inzidenz_value() {
  return letzte_inzidenz;
}

int32_t rundenGanzZahl(float input) {
  float temp = input - int(input);
  if (input >= 0) {
    temp = input - int(input);
  } else {
    temp = input + int(input);
  }
  if (temp >= 0.5f) {
    return int(input + 1);
  } else {
    return int(input);
  }
}

void parseJson(const char * jsonString) {
  /*  This function is used to parse JSON data
   *  Arguments:
   *  jsonString      constant char array containing the JSON data from OWM API
   *  
   *  Returns:
   *  nothing
   */
  
  //StaticJsonDocument<4000> doc;
  const size_t bufferSize = 2 * JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(2) + 4 * JSON_OBJECT_SIZE(1) + 3 * JSON_OBJECT_SIZE(2) + 3 * JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 2 * JSON_OBJECT_SIZE(7) + 2 * JSON_OBJECT_SIZE(8) + 720;
  DynamicJsonDocument doc(bufferSize);

  // FIND FIELDS IN JSON TREE
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    // deserializeJson() failed
    return;
  }

  // variables for JSON data
  JsonArray list = doc["features"];
  JsonObject current = list[0];

  String city = current["attributes"]["GEN"];
  uint32_t timezone = 7200;
  float inzidenz = current["attributes"]["cases7_per_100k"];
  float geheilte = current["attributes"]["geheilte"];
  float aktive = current["attributes"]["aktive"];
  unsigned long stand = current["attributes"]["stand"];
  int32_t tote = current["attributes"]["tote"];
  int32_t neumeldungen = current["attributes"]["f12"];

  letzte_inzidenz = (uint16_t)rundenGanzZahl(inzidenz);
  
}

void makehttpRequest() {
  /*  This function is used to make http request to request data from OWM API
   *  Arguments:
   *  none
   *  
   *  Returns:
   *  nothing
   */
  
  // close any connection before send a new request to
  // allow client make connection to server
  client.stop();

  // if there is a successful connection:
  if (client.connect(server, 443)) {
    // send the HTTP PUT request:
    // https://www.arcgishostedserver.nrw.de/arcgis/rest/services/Hosted/COVID19_Indikatoren_Kreise/FeatureServer/0/query?f=json&where=objectid=29&returnGeometry=false&spatialRel=esriSpatialRelIntersects&outFields=objectid,gn,stand,infizierte,tote,geheilte,inzidenz,aktive,f12&orderByFields=inzidenz%20desc&resultOffset=0
    String t1 = "GET /mOBPykOjAyBO2ZKk/arcgis/rest/services/RKI_Landkreisdaten/FeatureServer/0/query?where=1%3D1&outFields=cases7_per_100k,GEN,objectId&outSR=4326&f=json&returnGeometry=false&objectIds=" + object_id + " HTTP/1.1";
    String t2 = "Host: services7.arcgis.com"; 
    String t3 = "User-Agent: ArduinoWiFi/1.1"; 
    String t4 = "Connection: close";
    client.println(t1); 
    client.println(t2); 
    client.println(t3); 
    client.println(t4); 
    client.println();

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        // >>> Client Timeout !
        client.stop();
        return;
      }
    }

    char c = 0;
    while (client.available()) {
      c = client.read();
      // Client Readout!
      /* since json contains equal number of open and
       * close curly brackets, this means we can determine
       * when a json is completely received  by counting
       * the open and close occurences,
       */ 
      if (c == '{') {
        startJson = true; // set startJson true to indicate json message has started
        jsonend++;
      }
      if (c == '}') {
        jsonend--;
      }
      if (startJson == true) {
        text += c;
      }
      // if jsonend = 0 then we have received equal number of curly braces
      if (jsonend == 0 && startJson == true) {
        parseJson(text.c_str());  // parse c string text in parseJson function
        text = "";                // clear text string for the next time
        startJson = false;        // set startJson to false to indicate that a new message has not yet started
      }
    }
  }
  else {
    // Connection failed!
    return;
  }
}

void setup_corona_api(uint16_t objectId) {
  object_id = String(objectId);
  client.setCACert(sslCertificate);
}
