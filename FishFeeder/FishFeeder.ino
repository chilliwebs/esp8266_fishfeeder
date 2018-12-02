#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266LLMNR.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266SSDP.h>
#include <ESP8266mDNS.h>
#include <time.h>
#include <Stepper.h>

extern const char *FISH_SVG;
extern const char *CSS;
extern const char *JS;
const char *page_template(const char *title, const char *content);

const byte DNS_PORT = 53;
const int stepsPerRevolution = 2048;  // change this to fit the number of steps per revolution for your motor
int motorSpeed = 15; //15
const unsigned char DAYFLAGS[7] = {1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6};
char ssid[32] = "";
char password[32] = "";
bool apmode = false;
IPAddress apIP(192, 168, 1, 1);
IPAddress mask(255, 255, 255, 0);
const char *softap_ssid = "fish-feeder";
const char *device_name = "Automatic Fish Feeder";

int timezone = 0;
int dst = 0;
time_t pwr_on = 0;
time_t last = 0;
int days1 = 0;
int time1 = 0;
int count = 0;

Stepper myStepper(stepsPerRevolution, 0, 2, 1, 3);
DNSServer dnsServer;
ESP8266WebServer server(80);

time_t getNext(int days1, int time1, time_t now) {
  // sun=0, mon=1, tue=2, wed=3, thu=4, fri=5, sat=6
  uint8_t curr_dow = ((now / (86400)) + 3) % 7;
  uint8_t done = curr_dow + 7;
  // starting with today check each day of the week untill we get a match
  for (uint8_t day = curr_dow; day <= done; day++) {
    if (days1 & DAYFLAGS[day % 7]) {
      int next_time = (((now / 86400) + (day - curr_dow)) * 86400) + (long)(time1 * 60);
      if (next_time >= now)
        return next_time;
    }
  }
  return 0;
}

void feed(void) {
  time_t now = time(nullptr);
  last = now;
  myStepper.step(stepsPerRevolution / 16);
  delay(500);
  digitalWrite(0, LOW);
  digitalWrite(1, LOW);
  digitalWrite(2, LOW);
  digitalWrite(3, LOW);
  count++;
}

void setup(void) {
  pinMode(0, OUTPUT);
  pinMode(1, OUTPUT);
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  digitalWrite(0, LOW);
  digitalWrite(1, LOW);
  digitalWrite(2, LOW);
  digitalWrite(3, LOW);

  int EOE = 0;
  EEPROM.begin(76);
  EEPROM.get(0, ssid);
  EEPROM.get(32, password);
  EEPROM.get(64, days1);
  EEPROM.get(68, time1);
  EEPROM.get(72, EOE);
  if (EOE != 0x0E0E) {
    days1 = 0;
    time1 = 0;
    ssid[0] = '\0';
    password[0] = '\0';
    EEPROM.put(0, ssid);
    EEPROM.put(32, password);
    EEPROM.put(64, days1);
    EEPROM.put(68, time1);
    EEPROM.put(72, 0x0E0E);
    EEPROM.commit();
  }
  EEPROM.end();

  WiFi.hostname(softap_ssid);
  if (strlen(ssid) > 0 && strlen(password) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    // Wait for connection
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 120) { // 60s
      delay(500);
      tries++;
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(softap_ssid);
    delay(100);
    WiFi.softAPConfig(apIP, apIP, mask);
    dnsServer.setTTL(300);
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
    dnsServer.start(DNS_PORT, String(softap_ssid) + ".local", apIP);
    apmode = true;
  }

  if(!apmode) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LLMNR.begin(softap_ssid);
    MDNS.begin(softap_ssid);
    MDNS.addService("http", "tcp", 80);

    String url = "http://";
    url += softap_ssid;
    url += ".local/";

    String model = device_name;
    model += " 2018";
    
    SSDP.setSchemaURL("schema.xml");
    SSDP.setHTTPPort(80);
    SSDP.setName(device_name);
    SSDP.setSerialNumber("HE297126108946");
    SSDP.setURL(url);
    SSDP.setModelName(model);
    SSDP.setModelNumber("HE-WAFF-US1");
    SSDP.setModelURL(url);
    SSDP.setManufacturer("Hecht Electronics");
    SSDP.setManufacturerURL("https://www.linkedin.com/in/nick-hecht-41627a63/");
    SSDP.setDeviceType("urn:schemas-upnp-org:device:chilliwebs-esp8266-fishfeeder:1");
    SSDP.begin();
  }
  server.on("/schema.xml", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-stale=31536000, max-age=31536000");
    SSDP.schema(server.client());
  });
  server.on("/", []() {
    String content = "<header><img id=\"fishicon\" src=\"fish.svg\"><h1>";
    content += device_name;
    content += R"(</h1><hr/>
<span id='uptime'>loading...</span>
<span id='currenttime'>loading...</span></header>
<section id='config' style='display: none;'>
<h2>Configure Wifi:</h2><br/>
<h4>SSID: <input id='ssid' size='31'/></h4>
<h4>Password: <input id='password' size='31'/></h4><br/>
<button id='save' onclick='save_config()'>save</button>
</section>
<section id='feeding' style='display: none;'><h2>Feedings:</h2><br/>
<h4>Count: <span id='count'>loading...</span></h4>
<h4>Last: <span id='last'>loading...</span></h4>
<h4>Next: <span id='next'>loading...</span></h4><br/>
<h3>Schedule:</h3>
<h4>1: <input id='time1' type='time' size='5' value='00:00'/> 
<label><input type='checkbox' name='days1[]' value='1' /> Mon</label> 
<label><input type='checkbox' name='days1[]' value='2' /> Tue</label> 
<label><input type='checkbox' name='days1[]' value='4' /> Wed</label> 
<label><input type='checkbox' name='days1[]' value='8' /> Thu</label> 
<label><input type='checkbox' name='days1[]' value='16' /> Fri</label> 
<label><input type='checkbox' name='days1[]' value='32' /> Sat</label> 
<label><input type='checkbox' name='days1[]' value='64' /> Sun</label> 
</h4><br/>
<button id='save' onclick='save()'>save</button>
</section>)";
  server.sendHeader("Cache-Control", "max-stale=31536000, max-age=31536000");
  server.send(200, "text/html", page_template(device_name, content.c_str()));
  });
  server.on("/style.css", []() {
    server.sendHeader("Cache-Control", "max-stale=31536000, max-age=31536000");
    server.send(200, "text/css", CSS);
  });
  server.on("/script.js", []() {
    server.sendHeader("Cache-Control", "max-stale=31536000, max-age=31536000");
    server.send(200, "application/javascript", JS);
  });
  server.on("/fish.svg", []() {
    server.sendHeader("Cache-Control", "max-stale=31536000, max-age=31536000");
    server.send(200, "image/svg+xml", FISH_SVG);
  });
  server.on("/restart", []() {
    ssid[0] = '\0';
    password[0] = '\0';
    days1 = 0;
    time1 = 0;
    server.send(200, "text/plain", "OK");
    server.close();
    delay(1000);
    ESP.restart();
  });
  server.on("/reset", []() {
    ssid[0] = '\0';
    password[0] = '\0';
    days1 = 0;
    time1 = 0;
    EEPROM.begin(76);
    EEPROM.put(0, ssid);
    EEPROM.put(32, password);
    EEPROM.put(64, days1);
    EEPROM.put(68, time1);
    EEPROM.commit();
    EEPROM.end();
    server.send(200, "text/plain", "OK");
    server.close();
    delay(1000);
    ESP.restart();
  });
  server.on("/feed", []() {
    feed();
    time_t now = time(nullptr);
    EEPROM.begin(76);
    EEPROM.get(64, days1);
    EEPROM.get(68, time1);
    EEPROM.end();
    time_t next = getNext(days1, time1, now);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    String msg = "{\"on\":";
    msg += String(pwr_on, DEC);
    msg += ",\"now\":";
    msg += String(now, DEC);
    msg += ",\"next\":";
    msg += String(next, DEC);
    msg += ",\"last\":";
    msg += String(last, DEC);
    msg += ",\"days1\":";
    msg += String(days1, DEC);
    msg += ",\"time1\":";
    msg += String(time1, DEC);
    msg += ",\"count\":";
    msg += String(count, DEC);
    msg += ",\"apmode\":";
    msg += String(apmode, DEC);
    msg += "}";
    server.send(200, "application/json", msg);
  });
  server.on("/info", []() {
    time_t now = time(nullptr);
    EEPROM.begin(76);
    EEPROM.get(64, days1);
    EEPROM.get(68, time1);
    EEPROM.end();
    time_t next = getNext(days1, time1, now);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    String msg = "{\"on\":";
    msg += String(pwr_on, DEC);
    msg += ",\"now\":";
    msg += String(now, DEC);
    msg += ",\"next\":";
    msg += String(next, DEC);
    msg += ",\"last\":";
    msg += String(last, DEC);
    msg += ",\"days1\":";
    msg += String(days1, DEC);
    msg += ",\"time1\":";
    msg += String(time1, DEC);
    msg += ",\"count\":";
    msg += String(count, DEC);
    msg += ",\"apmode\":";
    msg += String(apmode, DEC);
    msg += "}";
    server.send(200, "application/json", msg);
  });
  server.on("/save", []() {
    if (server.arg("ssid") != "" || server.arg("password") != "") {
      server.arg("ssid").toCharArray(ssid, 32);
      server.arg("password").toCharArray(password, 32);
      EEPROM.begin(76);
      EEPROM.put(0, ssid);
      EEPROM.put(32, password);
      EEPROM.commit();
      EEPROM.end();
      server.send(200, "text/plain", "OK");
      server.close();
      delay(1000);
      ESP.restart();
    } else if (server.arg("days1") != "" || server.arg("time1") != "") {
      days1 = atoi(server.arg("days1").c_str());
      time1 = atoi(server.arg("time1").c_str());
      EEPROM.begin(76);
      EEPROM.put(64, days1);
      EEPROM.put(68, time1);
      EEPROM.commit();
      EEPROM.end();
      String msg = "{\"days1\":";
      msg += String(days1, DEC);
      msg += ",\"time1\":";
      msg += String(time1, DEC);
      msg += "}";
      server.send(200, "text/plain", msg);
    } else {
      server.send(500, "text/plain", "invalid arguments");
    }
  });
  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html","<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      WiFiUDP::stopAll();
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      Update.begin(maxSketchSpace);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
    yield();
  });
  server.onNotFound([]() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " ";
      message += server.argName(i);
      message += ": ";
      message += server.arg(i);
      message += "\n";
    }
    server.send(404, "text/plain", message);
  });
  server.begin();

  myStepper.setSpeed(motorSpeed);
}

void loop(void) {
  if (time(nullptr) > 1000000000 && pwr_on == 0)
    pwr_on = time(nullptr);
  if (apmode)
    dnsServer.processNextRequest();
  server.handleClient();
  if (!apmode) {
    time_t now = time(nullptr);
    time_t next = getNext(days1, time1, now);
    if (next == now && last != next) {
      feed();
    }
  }
}
