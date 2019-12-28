#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <DNSServer.h>
//for LED status
#include <Ticker.h>

Ticker ticker;

#ifndef LED_BUILTIN
#define LED_BUILTIN 13 // ESP32 DOES NOT DEFINE LED_BUILTIN
#endif

int LED = LED_BUILTIN;

void tick()
{
  //toggle state
  digitalWrite(LED, !digitalRead(LED)); // set pin to the opposite state
}

ESP8266WebServer server(80);

//Handles http request
void handleRoot()
{
  server.send(200, "text/plain", "hello from esp8266!");
}

// increase / decrease the blink rate. seconds (float)
void handleModifyBlinkRate()
{
  Serial.println("handleModifyBlinkRate called.");

  if (server.arg("seconds") == "")
  {
    server.send(200, "text/plain", "please pass the seconds argument in the query");
    return;
  }

  ticker.detach();
  ticker.attach(server.arg("seconds").toFloat(), tick);

  server.send(200, "text/plain", "modified blink rate to " + server.arg("seconds") + " seconds");
}

// handle not found
void handleNotFound()
{
  String message = "API Endpoint Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += server.method();
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

//gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void setup()
{
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  Serial.begin(115200);

  //set build in LED pin as output
  pinMode(LED, OUTPUT);
  // when connection is pending, the led blinks rapidly
  ticker.attach(0.5, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;
  //reset settings - for testing
  //wm.resetSettings();

  //Se the AP Static IP
  IPAddress _ip = IPAddress(10, 1, 104, 1);
  IPAddress _gw = IPAddress(10, 1, 104, 1);
  IPAddress _sn = IPAddress(255, 255, 255, 0);
  wm.setAPStaticIPConfig(_ip, _gw, _sn);

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wm.setAPCallback(configModeCallback);
  wm.setScanDispPerc(true);
  wm.setConfigPortalTimeout(180);

  //wm.startWebPortal();

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wm.autoConnect("MyIOTProject"))
  {
    Serial.println("Failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("Connected successfully to the WiFi");
  ticker.detach();
  //then it's connected, the LED is on
  digitalWrite(LED, LOW);

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!MDNS.begin("esp8266"))
  {
    Serial.println("Error setting up MDNS responder!");
    while (1)
    {
      delay(1000);
    }
  }

  Serial.println("mDNS responder started");

  // Start TCP (HTTP) server
  server.begin();
  Serial.println("TCP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/blink_rate", handleModifyBlinkRate);

  // not found
  server.onNotFound(handleNotFound);
  server.begin(); //Start server
  Serial.println("HTTP server started");
}

void loop(void)
{
  MDNS.update();
  server.handleClient();
}
