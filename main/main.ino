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

#define HOSTNAME "ESP_Planter"

String ssid;
String password;
String hostname = HOSTNAME;
String group = "/office";
String mqttServer;
String clientID = HOSTNAME;
String mqttUsername = "homeassistant";
String mqttPass = "tunoosa4jauvaiyakeit1aif9ahthieH1EiZ5Aol3Bee2xahx4rubiedoohae6Ac";
int count;

String device_topic = group + "/devices";

int maxDevices = 32;
int numDevices = 1;
String str[32] = {HOSTNAME};
int tmp[32];
int hmd[32];
int light[32];
int water[32];

IPAddress IP;

// TCP server at port 80 will respond to HTTP requests
ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup(void) {
    Serial.begin(115200);
    Serial.println("");
    LittleFS.begin();

    String config_data = load_from_file("data.conf");
    if (config_data != "") {
        int index = config_data.indexOf(",");
        hostname= config_data.substring(0, index);
        mqttServer = config_data.substring(index+1, config_data.length());
        mqttServer = "192.168.253.76";
    }

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
    if (count % 1000 == 0) {
        updateSensors();
    }
    count++;
}

void setup_wifi() {
    String file = load_from_file("wifi.conf");

    if (file != "") {
        Serial.println("Connecting to WiFi");
        int index = file.indexOf(":");
        int len = file.length();
        ssid = file.substring(0, index);
        password = file.substring(index + 1, len);
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
    server.begin();
    Serial.println("TCP server started");
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
    }
    save_all();
    server.send(200, "text/plain", "DONE");
}

void save_all() {
    String output = ssid + ":" + password;
    write_to_file("wifi.conf", output);

    output = hostname + "," + mqttServer;
    write_to_file("data.conf", output);
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
        for (int i = 0; i < maxDevices; i++) {
            // Tokenise and compare against the known devices
            // AND what sensor we are reciveving
            if (device == str[i]) {
                if (sensor == "temp") {
                    Serial.println("TEMP");
                    tmp[i] = msg.toInt();

                } else if (sensor == "hmd") {
                    hmd[i] = msg.toInt();

                } else if (sensor == "light") {
                    light[i] = msg.toInt();

                } else if (sensor == "water") {
                    water[i] = msg.toInt();

                }
            }
        }
    }
}

void addDeviceName(String msg) {
    // Add to devices
    bool found = false;
    for (int i = 0; i < maxDevices; i++) {
        if (str[i] == msg) {
            Serial.println("FOUND");
            found = true;
        }
    }
    if (found == false) {
        if (numDevices >= maxDevices) {
            Serial.println("Cannot add any more devices");
        } else {
            Serial.println("ADDING");
            str[numDevices] = msg;
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
    while(!mqttClient.connected()) {
        if (mqttClient.connect(clientID.c_str(), mqttUsername.c_str(), mqttPass.c_str())) {
            Serial.println("MQTT connected");
            mqttClient.subscribe((group + "/plant1/temp").c_str());
            mqttClient.subscribe(device_topic.c_str());
            mqttClient.publish((group + "/plant1/temp").c_str(), "1000");
        } else {
            Serial.println("Failed to connect to mqtt trying again");
            delay(5000);
        }
    }
}


void updateSensors() {

    // pubSensorData(tmp, hmd, light, water);
}


void pubSensorData(int tmp, int hmd, int light, int water) {
    String tmpTopic = group + "/" + hostname + "/temp";
    String hmdTopic = group + "/" + hostname + "/hmd";
    String lightTopic = group + "/" + hostname + "/light";
    String waterTopic = group + "/" + hostname + "/water";

    // mqttClient.publish(tmpTopic, tmp);
    // mqttClient.publish(hmdTopic, hmd);
    // mqttClient.publish(lightTopic, light);
    // mqttClient.publish(waterTopic, water);
}
