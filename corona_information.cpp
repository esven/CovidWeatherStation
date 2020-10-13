#include <ArduinoJson.h>  // for parsing JSON files
#include <WiFi.h>         // for connecting to the local WiFi network
#include <WiFiClientSecure.h>   // for accessing NTP real time API data

#include "corona_information.h"

// Open Weather Map API server name
const char server[] = "www.arcgishostedserver.nrw.de"; // server name
// Ennepe-Ruhr-Kreis = 29
// Remscheid = 8
String object_id; // city name and country code
const char sslCertificate[] PROGMEM = R"=====(
-----BEGIN CERTIFICATE-----
MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/
MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT
DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow
PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD
Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB
AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O
rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq
OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b
xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw
7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD
aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV
HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG
SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69
ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr
AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz
R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5
JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo
Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ
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

  String city = current["attributes"]["gn"];
  uint32_t timezone = 7200;
  float inzidenz = current["attributes"]["inzidenz"];
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
    String t1 = "GET /arcgis/rest/services/Hosted/COVID19_Indikatoren_Kreise/FeatureServer/0/query?f=json&where=objectid=" + object_id + "&returnGeometry=false&spatialRel=esriSpatialRelIntersects&outFields=objectid,gn,stand,infizierte,tote,geheilte,inzidenz,aktive,f12&orderByFields=inzidenz%20desc&resultOffset=0 HTTP/1.1";
    String t2 = "Host: www.arcgishostedserver.nrw.de"; 
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
