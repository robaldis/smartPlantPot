/*
   ESP8266 mDNS responder sample

   This is an example of an HTTP server that is accessible
   via http://esp8266.local URL thanks to mDNS responder.

Instructions:
- Update WiFi SSID and password as necessary.
- Flash the sketch to the ESP8266 board
- Install host software:
- For Linux, install Avahi (http://avahi.org/).
- For Windows, install Bonjour (http://www.apple.com/support/bonjour/).
- For Mac OSX and iOS support is built in through Bonjour already.
- Point your browser to http://esp8266.local, you should see a response.

 */


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include "header.h"
#include "LittleFS.h"
#include <PubSubClient.h>
#include <Vector.h>
#include <DHT.h>

#define HOSTNAME "ESP_Planter"
#define DHTType DHT11

int DHTPin = D6;
int LEDPin = D0;
int lightPin = A0;
int waterPin = D7;

DHT dht(DHTPin, DHT11);

String ssid;
String password;
String hostname = HOSTNAME;
String group;
String mqttServer;
String clientID;
String mqttUsername;
String mqttPass;
// "homeassistant"
// "tunoosa4jauvaiyakeit1aif9ahthieH1EiZ5Aol3Bee2xahx4rubiedoohae6Ac"
String device_topic;
int count;

int maxDevices = 32;
int numDevices = 1;
String deviceNames[32] = {HOSTNAME};
int temp_arr[32];
int hmd_arr[32];
int light_arr[32];
int water_arr[32];

IPAddress IP;

// TCP server at port 80 will respond to HTTP requests
ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup(void) {
    Serial.begin(115200);
    Serial.println("");
    LittleFS.begin();
    dht.begin();
    pinMode(LEDPin, OUTPUT);

    String config_data = load_from_file("config");
    if (config_data != "") {
        int index = config_data.indexOf(":");
        hostname= config_data.substring(0, index);
        clientID = hostname;
        deviceNames[0] = hostname;

        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        ssid = config_data.substring(0, index);
        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        password = config_data.substring(0, index);
        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        group = config_data.substring(0, index);
        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        mqttServer = config_data.substring(0, index);
        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        mqttUsername = config_data.substring(0, index);
        config_data= config_data.substring(index+1);

        mqttPass = config_data.substring(0);
    }
    device_topic = group + "/devices";

    Serial.println(group);
    Serial.println(hostname);

    setup_wifi();
    setup_http_server();
    setup_mdns();
    setup_mqtt();

}

void loop(void) {
    MDNS.update();
    server.handleClient();
    if (!mqttClient.connected()) {
        mqttReconnect();
    }
    mqttClient.loop();

    if (count > 500000) {
        mqttClient.publish(device_topic.c_str(), clientID.c_str());
        count = 0;
        updateSensors();
    }
    count++;
}

void setup_wifi() {
    if (ssid != "") {
        Serial.println("Connecting to WiFi");
        // Connect to WiFi network
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        // Wait for connection
        int count = 0;
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            if(count > 60) {
                connect_ap();
                break;
            }
            count++;
        }
        IP = WiFi.localIP();
    } else {
        connect_ap();
    }

    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(IP);
}

void connect_ap() {
        Serial.println("Access point");
        ssid = hostname;
        WiFi.softAP(ssid);
        IP = WiFi.softAPIP();
        Serial.println(IP);
}

void setup_mdns() {
    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin(hostname, IP)) {
        Serial.println("Error setting up MDNS responder!");
        while (1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
}

void setup_http_server(){
    // Start TCP (HTTP) server
    server.on("/", handle_index);
    server.on("/wifi", wifi);
    server.on("/ap", ap);
    server.on("/setup", config_GET);
    server.on("/setup_form", config_POST);
    server.on("/reset", resetConfig);
    server.begin();
    Serial.println("TCP server started");
}

void resetConfig() {
    write_to_file("config","");
    server.send(200, "text/plain", "DONE");
}

void setup_mqtt() {
    mqttClient.setServer(mqttServer.c_str(), 1883);
    mqttClient.setCallback(mqttMessage);
}

void handle_index() {
    //Print Hello at opening homepage
    String index = load_from_file("index.html");
    server.send(200, "text/html", index);
}

void wifi() {
    write_to_file("config", "wifi");

    server.send(200, "text/plain", "Set to wifi");
}

void ap() {
    write_to_file("config", "ap");

    server.send(200, "text/plain", "Set to ap");

}

void config_GET() {
    String setup_html = load_from_file("setup.html");
    server.send(200, "text/html", setup_html);

}

void config_POST() {
    Serial.println("POST sent");
    for (uint8_t i = 0; i < server.args(); i++) { 
        if (server.argName(i) == "device_name") {
            hostname = server.arg(i);
        }
        else if (server.argName(i) == "mqttServer") {
            mqttServer = server.arg(i);
        }
        else if (server.argName(i) == "password") {
            password = server.arg(i);
        }
        else if (server.argName(i) == "ssid") {
            ssid = server.arg(i);
        }
        else if (server.argName(i) == "mqtt_ip") {
            mqttServer = server.arg(i);
        }
        else if (server.argName(i) == "mqtt_user") {
            mqttUsername = server.arg(i);
        }
        else if (server.argName(i) == "mqtt_pass") {
            mqttPass = server.arg(i);
        }
        else if (server.argName(i) == "group") {
            group = server.arg(i);
        }
    }
    save_all();
    server.send(200, "text/plain", "DONE");
}

void save_all() {
    String output = hostname + ":" + ssid + ":" + password + ":" + group + ":" + mqttServer + ":" + mqttUsername + ":" + mqttPass;
    write_to_file("config", output);
}


String load_from_file(String file_name) {
    String result = "";

    File this_file = LittleFS.open(file_name, "r");
    if (!this_file) { // failed to open the file, retrn empty result
        return result;
    }
    while (this_file.available()) {
        result += (char)this_file.read();
    }

    this_file.close();
    return result;
}
bool write_to_file(String file_name, String contents) {  
    File this_file = LittleFS.open(file_name, "w");
    if (!this_file) { // failed to open the file, return false
        return false;
    }
    int bytesWritten = this_file.print(contents);

    if (bytesWritten == 0) { // write failed
        return false;
    }

    this_file.close();
    return true;
}

void mqttMessage(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("    ");
    Serial.print(length);
    Serial.print("]: ");
    String msg;
    for (int i = 0; i < length; i++) {
        msg = msg + (char)payload[i];
    }
    Serial.print(msg);
    Serial.println("");

    if (strstr(topic, device_topic.c_str())) {
        addDeviceName(msg);
    } else {
        String s = String(topic).substring(1);
        int index = s.indexOf("/");
        String group = s.substring(0, index);
        s = s.substring(index+1);
        index = s.indexOf("/");
        String device = s.substring(0, index);
        String sensor = s.substring(index+1);
        for (int i = 1; i < maxDevices; i++) {
            // Tokenise and compare against the known devices
            // AND what sensor we are reciveving
            if (device == deviceNames[i]) {
                if (sensor == "temp") {
                    Serial.println("TEMP");
                    temp_arr[i] = msg.toInt();

                } else if (sensor == "hmd") {
                    hmd_arr[i] = msg.toInt();

                } else if (sensor == "light") {
                    light_arr[i] = msg.toInt();

                } else if (sensor == "water") {
                    water_arr[i] = msg.toInt();

                }
            }
        }
    }
}

void addDeviceName(String msg) {
    // Add to devices
    bool found = false;
    for (int i = 0; i < maxDevices; i++) {
        if (deviceNames[i] == msg) {
            Serial.println("FOUND");
            found = true;
        }
    }
    if (found == false) {
        if (numDevices >= maxDevices) {
            Serial.println("Cannot add any more devices");
        } else {
            Serial.println("ADDING");
            deviceNames[numDevices] = msg;
            numDevices++;
            Serial.println(msg);
            subscribeToDevice(msg);
        }
    }
}


void subscribeToDevice(String device) {
    String topic = group + "/" + device + "/#";
    mqttClient.subscribe(topic.c_str());
}


void mqttReconnect() {
    if (mqttClient.connect(clientID.c_str(), mqttUsername.c_str(), mqttPass.c_str())) {
        Serial.println("MQTT connected");
        mqttClient.subscribe(device_topic.c_str());
    } else {
        Serial.println("Failed to connect to mqtt trying again");
        delay(5000);
    }
}


void updateSensors() {
    int temp = dht.readTemperature(0);
    int hmd = dht.readHumidity();
    int light = analogRead(lightPin);
    int water = analogRead(waterPin);
    Serial.print("TEMP: ");
    Serial.println(temp);
    Serial.print("HUMIDITY: ");
    Serial.println(hmd);
    Serial.print("LIGHT: ");
    Serial.println(light);
    Serial.print("WATER: ");
    Serial.println(water);

    temp_arr[0] = temp;
    hmd_arr[0] = hmd;
    light_arr[0] = light;
    water_arr[0] = water;

    if (water == 0) {
        digitalWrite(LEDPin, LOW);
    } else {
        digitalWrite(LEDPin, HIGH);
    }

    pubSensorData(temp, hmd, light, water);
}


void pubSensorData(int tmp, int hmd, int light, int water) {
    String tmpTopic = group + "/" + hostname + "/temp";
    String hmdTopic = group + "/" + hostname + "/hmd";
    String lightTopic = group + "/" + hostname + "/light";
    String waterTopic = group + "/" + hostname + "/water";

    mqttClient.publish(tmpTopic.c_str(), String(tmp).c_str());
    mqttClient.publish(hmdTopic.c_str(), String(hmd).c_str());
    mqttClient.publish(lightTopic.c_str(), String(light).c_str());
    mqttClient.publish(waterTopic.c_str(), String(water).c_str());
}
