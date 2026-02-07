// Import required libraries
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>  // ---- BMP180 ----
#include <PubSubClient.h>

// Replace with your network credentials
const char* ssid = "MVT-TONY";
const char* password = "<tony0889>";

const char* mqtt_user = "esp32_sensor";  
const char* mqtt_pass = "123";    
const char* mqtt_server = "192.168.0.11"; // IP do laptop servidor

#define DHTPIN 12     // D6 Digital pin connected to the DHT sensor
#define DHTTYPE DHT11

#define DHT22_PIN D5    // D6 Digital pin connected to the DHT sensor
#define DHT22_TYPE DHT22

DHT dht(DHTPIN, DHTTYPE);
DHT dht22(DHT22_PIN, DHT22_TYPE);

Adafruit_BMP085 bmp;  // ---- BMP180 ----

// current sensor readings
float t = 0.0;
float h = 0.0;
float t_dht22 = 0.0;
float h_dht22 = 0.0;
float p = 0.0;  // pressure (hPa)
float alt = 0.0; // altitude (m)

AsyncWebServer server(80);

unsigned long previousMillis = 0;
const long interval = 10000;  // 10s update

WiFiClient espClient;
PubSubClient client(espClient);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>html{font-family:Arial;text-align:center;}</style>
</head>
<body>
  <h2>ESP8266 Weather Server</h2>
  <p>Temp: <span id="temperature">%TEMPERATURE%</span> °C</p>
  <p>Humidity: <span id="humidity">%HUMIDITY%</span> %</p>
  <p>Pressure: <span id="pressure">%PRESSURE%</span> hPa</p>
  <p>Altitude: <span id="altitude">%ALTITUDE%</span> m</p>
</body>
<script>
function update(id,url){fetch(url).then(r=>r.text()).then(t=>document.getElementById(id).innerHTML=t);}
setInterval(()=>{update("temperature","/temperature");update("humidity","/humidity");update("pressure","/pressure");update("altitude","/altitude");},10000);
</script>
</html>)rawliteral";

// Replaces placeholder with sensor values
String processor(const String& var){
  if(var == "TEMPERATURE") return String(t, 2);
  if(var == "HUMIDITY") return String(h, 2);
  if(var == "PRESSURE") return String(p, 2);
  if(var == "ALTITUDE") return String(alt, 2);
  return String();
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect("esp8266_sensor", mqtt_user, mqtt_pass)) {
      Serial.println("Conectado!");
    } else {
      Serial.print("Falhou, rc="); Serial.println(client.state());
      delay(5000);
    }
  }
}

void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500); Serial.print(".");
    }
    Serial.println(WiFi.localIP());
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  dht22.begin();

  // ---- BMP180 - Barometric sensor----
  if (!bmp.begin()) {
    Serial.println("BMP180 has not been detected! Please verify pin connectivity");
    while (1);
  } else {
    Serial.println("BMP180 has been started!");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());

  // Web server routes for metrics collection
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(t).c_str()); });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(h).c_str()); });
  server.on("/pressure", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(p).c_str()); });
  server.on("/altitude", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(alt).c_str()); });
  server.begin();

  client.setServer(mqtt_server, 1883);
  reconnectMQTT();
}

void loop() {
  reconnectWiFi();
  if (!client.connected()) reconnectMQTT();
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    float newT = dht.readTemperature();
    float newH = dht.readHumidity();
    if (!isnan(newT)) t = newT;
    if (!isnan(newH)) h = newH;

    // ---- BMP180 readings ----
    p = bmp.readPressure() / 100.0; // convert Pa to hPa
    alt = bmp.readAltitude();       // meters (default sea level 1013.25 hPa)

    Serial.printf("Temp: %.2f°C | Umid: %.2f%% | Press: %.2f hPa | Alt: %.2f m\n", t, h, p, alt);

    newT = dht22.readTemperature();
    newH = dht22.readHumidity();
    if (!isnan(newT)) t_dht22 = newT;
    if (!isnan(newH)) h_dht22 = newH;
    
    Serial.printf("Temp (DHT22): %.2f°C | Umid: %.2f%% \n", t_dht22, h_dht22);

    // MQTT publish to broker
    String payload = "{\"temperature\":" + String(t, 2) +
                     ",\"humidity\":" + String(h, 2) +
                     ",\"dht22_temperature\":" + String(t_dht22, 2) +
                     ",\"dht22_humidity\":" + String(h_dht22, 2) +                     
                     ",\"pressure\":" + String(p, 2) +
                     ",\"altitude\":" + String(alt, 2) + "}";
    client.publish("casa/sala/sensor", payload.c_str(), true);
  }
}
