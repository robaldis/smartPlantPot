/*
   _____                      _     _____  _             _     _____      _   
  / ____|                    | |   |  __ \| |           | |   |  __ \    | |  
 | (___  _ __ ___   __ _ _ __| |_  | |__) | | __ _ _ __ | |_  | |__) |__ | |_ 
  \___ \| '_ ` _ \ / _` | '__| __| |  ___/| |/ _` | '_ \| __| |  ___/ _ \| __|
  ____) | | | | | | (_| | |  | |_  | |    | | (_| | | | | |_  | |  | (_) | |_ 
 |_____/|_| |_| |_|\__,_|_|   \__| |_|    |_|\__,_|_| |_|\__| |_|   \___/ \__|
  by Robert Aldis

 */

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include "LittleFS.h"
#include <PubSubClient.h>
#include <DHT.h>

#define HOSTNAME "ESP_Planter"
#define DHTType DHT11

// Define Pins
int DHTPin = D6;
int LEDPin = D0;
int lightPin = A0;
int waterPin = D7;

// Config data
String ssid;
String password;
String hostname = HOSTNAME;
String group;
String mqttServer;
String clientID;
String mqttUsername;
String mqttPass;

String device_topic;
int count; // Keep track of when to update sensors
bool needsSetup;
IPAddress IP;

// Device managment variables
int maxDevices = 3;
int numDevices = 1; // Starts at 1 it holds itself as a device
String deviceNames[maxDevices] = {HOSTNAME}; // keep track of itself
                                             // Lists that will hold sensor data for all devices
int temp_arr[maxDevices];
int hmd_arr[maxDevices];
int light_arr[maxDevices];
int water_arr[maxDevices];

// Setup librarys
DHT dht(DHTPin, DHT11);
ESP8266WebServer server(80); // port 80
WiFiClient espClient;
PubSubClient mqttClient(espClient);


void setup(void) {
    Serial.begin(115200);
    Serial.println("");
    LittleFS.begin();
    dht.begin();
    pinMode(LEDPin, OUTPUT);

    load_config("config");
    // Set the topic to find new devices on the network for its specific group
    device_topic = group + "/devices";

    // Print information
    Serial.println(group);
    Serial.println(hostname);
    Serial.println(needsSetup);

    // Setup networking
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

    // Update sensors over MQTT
    if (count > 500000) {
        mqttClient.publish(device_topic.c_str(), clientID.c_str());
        count = 0;
        updateSensors();
    }
    count++;
}


void load_config(file) {
    /*
    Load config data from a file and retreive data. config file NEEDS to be in 
    the correct format to be properly parsed.
    */
    String config_data = load_from_file(file);
    if (config_data != "") {
        int index = config_data.indexOf(":");
        hostname= config_data.substring(0, index);
        clientID = hostname;
        deviceNames[0] = hostname;
        needsSetup = checkEmpty(hostname);

        config_data= config_data.substring(index+1);

        index = config_data.indexOf(":");
        ssid = config_data.substring(0, index);
        config_data= config_data.substring(index+1);
        needsSetup = checkEmpty(ssid);

        index = config_data.indexOf(":");
        password = config_data.substring(0, index);
        config_data= config_data.substring(index+1);
        needsSetup = checkEmpty(password);

        index = config_data.indexOf(":");
        group = config_data.substring(0, index);
        config_data= config_data.substring(index+1);
        needsSetup = checkEmpty(group);

        index = config_data.indexOf(":");
        mqttServer = config_data.substring(0, index);
        config_data= config_data.substring(index+1);
        needsSetup = checkEmpty(mqttServer);

        index = config_data.indexOf(":");
        mqttUsername = config_data.substring(0, index);
        config_data= config_data.substring(index+1);
        needsSetup = checkEmpty(mqttUsername);

        mqttPass = config_data.substring(0);
    }
}


bool checkEmpty(String str) {
    /* 
       check if the string is empty or needsSetup flag is true
     */ 
    if (str == "" || needsSetup) {
        return true;
    } else {
        return false;

    }
}


void setup_wifi() {
    /*
       Setup the wifi to connect to the specified SSID using password. If it 
       cannot connect to the access point it will become its own access point
     */

    if (ssid != "") {
        Serial.println("Connecting to WiFi");
        // Connect to WiFi network
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        // Wait for connection
        int i = 0;
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            if(i > 60) {
                connect_ap();
                break;
            }
            i++;
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
    /*
       Setup access point to act as its own wifi point
     */
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
    /* 
    Start TCP (HTTP) server and all the endpoints
    */
    server.on("/", handle_index);
    server.on("/setup", config_GET);
    server.on("/setup_form", config_POST);
    server.on("/reset", resetConfig);
    server.begin();
    Serial.println("TCP server started");
}


void setup_mqtt() {
    /*
    Setup the MQTT server to connect to the broker 
    */
    mqttClient.setServer(mqttServer.c_str(), 1883);
    // Callback for when a message is recieved
    mqttClient.setCallback(mqttMessage); 
}

//---------------- HTTP requests -------------------

void handle_index() {
    /*
    Handles what happens when the root endpoint is reached
    */
    
    if (needsSetup) {
        // get to config settings page
        config_GET();
    } else {
        // Show index.html
        String index = load_from_file("index.html");
        server.send(200, "text/html", index);
    }
}


void config_GET() {
    /*
    Handles what happens when the setup endpoint is reached
    Show the setup.html to input the needed data to run the device 
    */
    String setup_html = load_from_file("setup.html");
    server.send(200, "text/html", setup_html);
}


void config_POST() {
    /*
    Handles what happens when the setup post endpoint is reached
    Sets all the values entered to variables, saves all the data into a config
    file
    */
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
    save_config();
    server.send(200, "text/plain", "DONE");
}


void resetConfig() {
    /*
    Reset the config data (factory reset)
    */
    // Write an empty string to the file clearing the contents
    write_to_file("config","");
    server.send(200, "text/plain", "DONE");
}

// --------------------------------------------------------------------

void save_config() {
    /* 
       Save config data and restart so everything can be setup with the new 
       information used.
     */
    String output = hostname + ":" + ssid + ":" + password + ":" + group + ":" + mqttServer + ":" + mqttUsername + ":" + mqttPass;
    write_to_file("config", output);
    void(* resetFunc) (void) = 0;
    resetFunc(); // Restart the device
}

// --------------------- File system -------------------------------

String load_from_file(String file_name) {
    /*
    Load the contents of a file into a string with a given file_name
    */
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
    /*
    Write the contents to a given file
    */
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

// --------------- MQTT ----------------------------------

void mqttMessage(char* topic, byte* payload, unsigned int length) {
    /*
    Every time a MQTT message is recived from a topic the device is subscirbed 
    to will be processed here
    */
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("    ");
    Serial.print(length);
    Serial.print("]: ");
    String msg;
    // Turn the message into a string
    for (int i = 0; i < length; i++) {
        msg = msg + (char)payload[i];
    }
    Serial.print(msg);
    Serial.println("");

    // check if the topic is for discovering new devices
    if (strstr(topic, device_topic.c_str())) {
        addDeviceName(msg);
    } else {
        // Tokenise the topic to get the device name and sensor type
        String s = String(topic).substring(1);
        int index = s.indexOf("/");
        String group = s.substring(0, index);
        s = s.substring(index+1);
        index = s.indexOf("/");
        String device = s.substring(0, index);
        String sensor = s.substring(index+1);

        // Find the index of the device we recieved and set the appropriate 
        // value to the new value
        for (int i = 1; i < maxDevices; i++) {
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
    /*
    If a new deivces is received add it to known devices to keep track of.
    */

    // Check if we already know about the device
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
            // Add devices to known devices we are tracking
            deviceNames[numDevices] = msg;
            numDevices++;
            Serial.println(msg);
            // Subscribe to new devices topic
            subscribeToDevice(msg);
        }
    }
}


void subscribeToDevice(String device) {
    /*
    Subscribe to a given topic
    */
    // Subscribe to all subtopics of a device to recived everything
    String topic = group + "/" + device + "/#";
    mqttClient.subscribe(topic.c_str());
}


void mqttReconnect() {
    /*
    Try reconnect to the mqtt protocol with the clientID mqttUsername and 
    mqttPass
    */
    if (mqttClient.connect(clientID.c_str(), mqttUsername.c_str(), mqttPass.c_str())) {
        Serial.println("MQTT connected");
        mqttClient.subscribe(device_topic.c_str());
    } else {
        Serial.println("Failed to connect to mqtt trying again");
        delay(5000);
    }
}


void pubSensorData(int tmp, int hmd, int light, int water) {
    /*
    Publish all the sensor data to the correct topics
    */
    // Setup topics for each sensor
    String tmpTopic = group + "/" + hostname + "/temp";
    String hmdTopic = group + "/" + hostname + "/hmd";
    String lightTopic = group + "/" + hostname + "/light";
    String waterTopic = group + "/" + hostname + "/water";

    // Publish sneosor data on each topic
    mqttClient.publish(tmpTopic.c_str(), String(tmp).c_str());
    mqttClient.publish(hmdTopic.c_str(), String(hmd).c_str());
    mqttClient.publish(lightTopic.c_str(), String(light).c_str());
    mqttClient.publish(waterTopic.c_str(), String(water).c_str());
}

// ------------------ Read sensors ---------------------------


void updateSensors() {
    /*
    Read sensor data and update variables
    */
    int temp = dht.readTemperature(0);
    int hmd = dht.readHumidity();
    int light = analogRead(lightPin);
    int water = digitalRead(waterPin);
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

    // Set LED indicator if water is out
    if (water == 0) {
        digitalWrite(LEDPin, LOW);
    } else {
        digitalWrite(LEDPin, HIGH);
    }

    // Publish over the MQTT protocol
    pubSensorData(temp, hmd, light, water);
}



