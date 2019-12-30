/*
 * ASYNC WEB SERVER, PERMANENT STORAGE, WIFI PORTAL, 2812 Leds, mDNS., task scheduler WHAT MORE COULD ONE WANT ?
*/

#include <FS.h> //this needs to be first, or it all crashes and burns...

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

//needed for library
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <WiFiClient.h>

// web server
#if defined(ESP8266)
#include <ESPAsyncTCP.h>
#else
#include <AsyncTCP.h>
#endif

// for storage
#include <ArduinoJson.h>

// FastLED
#define FASTLED_ESP8266_RAW_PIN_ORDER //D3 or GPIO0 is DATA_PIN 0
#include <FastLED.h>

// Other
#include <TaskScheduler.h> //https://github.com/arkhipenko/TaskScheduler/tree/master/examples

const char* PARAM_DEBUG = "debug";
const char* PARAM_WORK_DELAY = "work_delay";
const char* PARAM_REST_DELAY = "rest_delay";

/*
 * CONFIGURATION
 */
bool isDebug = true;
int workDelay = 20000;
int restDelay = 5000;
#define NUM_LEDS 12
#define DATA_PIN 0

CRGB leds[NUM_LEDS];
AsyncWebServer server(80);
DNSServer dns;
void taskUpdateMDNSCallback(); // prototype
void taskDoWorkCallback(); // prototype
void taskDoRestCallback(); // prototype

Task taskUpdateMDNS(1, TASK_FOREVER, &taskUpdateMDNSCallback);
Task taskDoWork(10000, TASK_FOREVER, &taskDoWorkCallback);
Task taskDoRest(10000, TASK_FOREVER, &taskDoRestCallback);
Scheduler runner;

void fadeall()
{
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
        FastLED.show();
    }
}

void workMode()
{
    fadeall();
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(170, 255, 255);
        FastLED.show();
        delay(workDelay);
        leds[i] = CRGB::Black;
        FastLED.show();
    }
}

void restMode()
{
    fadeall();
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(15, 255, 255);
        FastLED.show();
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        delay(restDelay);
        leds[i] = CRGB::Black;
        FastLED.show();
    }
}

bool loadConfig()
{
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println("Failed to open config file");
        return false;
    }

    size_t size = configFile.size();
    if (size > 1024) {
        Serial.println("Config file size is too large!");
        return false;
    }
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);

    StaticJsonDocument<200> doc;
    auto error = deserializeJson(doc, buf.get());
    if (error) {
        Serial.println("Failed to parse config file");
        return false;
    }

    isDebug = doc["debug"];
    workDelay = doc["work_delay"];
    restDelay = doc["rest_delay"];

    if (workDelay == 0) {
        workDelay = 500;
    }
    if (restDelay == 0) {
        restDelay = 500;
    }

    if (isDebug) {
        Serial.print("~~~~~~~~~~~~~~~~~~~~~~~~~~ DEBUG BUILD ~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        Serial.printf("Work Delay %d\n", workDelay);
        Serial.printf("Rest Delay %d\n", restDelay);
    } else {
        Serial.print("-------------------------- PRODUCTION BUILD --------------------------\n");
        Serial.printf("Work Delay %d\n", workDelay);
        Serial.printf("Rest Delay %d\n", restDelay);
    }
    return true;
}

// save config, also replaces the global settings
bool saveConfig(String _debug, int _workDelay, int _restDelay)
{
    Serial.println("saving new configuration");
    StaticJsonDocument<200> doc;

    if (_debug == "true") {
        doc["debug"] = true;
        isDebug = true;
    } else {
        doc["debug"] = false;
        isDebug = false;
    }
    doc["work_delay"] = _workDelay;
    doc["rest_delay"] = _restDelay;

    workDelay = _workDelay;
    restDelay = _restDelay;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return false;
    }

    serializeJson(doc, configFile);
    return true;
}

void taskUpdateMDNSCallback()
{
    MDNS.update();
}

void taskDoWorkCallback()
{
    taskDoWork.disable();
    workMode();
    taskDoRest.enable();
}

void taskDoRestCallback()
{
    taskDoRest.disable();
    restMode();
    taskDoWork.enable();
}

void notFound(AsyncWebServerRequest* request)
{
    request->send(404, "text/plain", "Not found");
}

void setup()
{
    // put your setup code here, to run once:
    delay(500);
    Serial.begin(115200);
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    if (!loadConfig()) {
        Serial.println("Failed to load config");
    } else {
        Serial.println("Config loaded");
    }

    FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(64); // 0-255 value

    runner.init(); // task scheduler init
    runner.addTask(taskDoWork);
    runner.addTask(taskDoRest);
    taskDoWork.enable();

    AsyncWiFiManager wifiManager(&server, &dns);
    //wifiManager.resetSettings();

    IPAddress _ip = IPAddress(10, 1, 104, 1);
    IPAddress _gw = IPAddress(10, 1, 104, 1);
    IPAddress _sn = IPAddress(255, 255, 255, 0);
    wifiManager.setAPStaticIPConfig(_ip, _gw, _sn);

    if (!wifiManager.autoConnect("PomodoroX")) {
        Serial.println("failed to connect, we should reset as see if it connects");
        delay(3000);
        ESP.reset();
        delay(5000);
    }

    Serial.println("connected successfully to the WiFi");

    Serial.println("local ip");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("pomodorox")) {
        Serial.println("Error setting up MDNS responder!");
        while (1) {
            delay(1000);
        }
    }

    Serial.println("mDNS responder started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("iotdevice", "tcp", "app_name", "pomodorox");
    MDNS.addServiceTxt("iotdevice", "tcp", "app_version", "0.0.1");

    // web server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "server alive");
    });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
        String debugAux = "";
        String workDelayAux = "";
        String restDelayAux = "";
        if (request->hasParam(PARAM_DEBUG) && request->hasParam(PARAM_REST_DELAY) && request->hasParam(PARAM_WORK_DELAY)) {
            debugAux = request->getParam(PARAM_DEBUG)->value();
            workDelayAux = request->getParam(PARAM_WORK_DELAY)->value();
            restDelayAux = request->getParam(PARAM_REST_DELAY)->value();

            saveConfig(debugAux, workDelayAux.toInt(), restDelayAux.toInt());

        } else {
            request->send(400, "text/plain", "Please make sure you include ALL the query parameters in the requesty");
            return;
        }
        request->send(200, "text/plain", "settings saved");
    });

    server.onNotFound(notFound);
    server.begin();

    runner.addTask(taskUpdateMDNS);
    taskUpdateMDNS.enable();
}

void loop()
{
    runner.execute(); // check the tasks for more
}
