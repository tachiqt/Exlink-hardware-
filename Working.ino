#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>


const char* ssid = "tachi";
const char* password = "qtkooooo";

#define SOCKET_ON LOW
#define SOCKET_OFF HIGH

ESP8266WebServer server(80);
Ticker watchdogTicker;

const int relay1 = 5;   // D1
const int relay2 = 4;   // D2
const int relay3 = 14;  // D5
const int relay4 = 12;  // D6

const int sc_pin = 16;  // D0  
const int s1_pin = 0;   // D3  
const int s2_pin = 2;   // D4  
const int s3_pin = 15;  // D8
const int s4_pin = 13;  // D7


bool socket1State = false;
bool socket2State = false;
bool socket3State = false;
bool socket4State = false;

int socket1Power = 0;
int socket2Power = 0;
int socket3Power = 0;
int socket4Power = 0;


#define MAX_LOGS 5
String requestLogs[MAX_LOGS]; 

int logIndex = 0;

void setupRoutes();
void addLog(const String& message);
void printStatus();
void controlSocket(int id, bool state);
void processSerialCommand(const String& command);
void handleRoot();
void handleSocketCommand();
void handleStatus();
void handlePowerStatus();
void handleTest();
void handleReset();
void updatePowerReadings();
int getPowerForSocket(int socketId);
void testSockets(bool quickTest = false);
bool readSensor(int pin);

void setup() {
    delay(2000);
    Serial.begin(115200);
    Serial.flush();
    delay(100);

    Serial.println();
    Serial.println("============================");
    Serial.println("ESP8266 SOCKET CONTROLLER");
    Serial.println("============================");
    Serial.printf("Free heap at start: %d bytes\n", ESP.getFreeHeap());

    pinMode(relay1, OUTPUT);
    pinMode(relay2, OUTPUT);
    pinMode(relay3, OUTPUT);
    pinMode(relay4, OUTPUT);

    digitalWrite(relay1, SOCKET_OFF);
    digitalWrite(relay2, SOCKET_OFF);
    digitalWrite(relay3, SOCKET_OFF);
    digitalWrite(relay4, SOCKET_OFF);

   
    pinMode(sc_pin, INPUT_PULLUP);
    pinMode(s1_pin, INPUT_PULLUP); 
    pinMode(s2_pin, INPUT_PULLUP); 
    pinMode(s3_pin, INPUT);   
    pinMode(s4_pin, INPUT_PULLUP); 

    Serial.println("All sockets initialized to OFF state");
    Serial.println("All pins configured");
    WiFi.disconnect(true); 
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("Connecting to WiFi: %s ", ssid);

    int timeout = 0;
    int maxAttempts = 40; 
    while (WiFi.status() != WL_CONNECTED && timeout < maxAttempts) {
        delay(500);
        Serial.print(".");
        timeout++;

        if (timeout % 10 == 0) {
            Serial.println();
            Serial.printf("Still trying to connect... Attempt %d/%d\n", timeout, maxAttempts);
            Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
            yield();
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFAILED TO CONNECT TO WIFI!");
        Serial.println("Starting access point mode instead");
        WiFi.disconnect();
        delay(1000);

        String apName = "ESP_Socket_" + String(ESP.getChipId());
        WiFi.softAP(apName.c_str(), "12345678");
        Serial.print("AP Mode active. SSID: ");
        Serial.println(apName);
        Serial.print("IP Address: ");
        Serial.println(WiFi.softAPIP().toString());
    } else {
        Serial.println("\n\n***** WIFI CONNECTED *****");
        Serial.println("\n==================================");
        Serial.println("IP ADDRESS: " + WiFi.localIP().toString());
        Serial.println("==================================\n");
    }
    setupRoutes();
    watchdogTicker.attach(60, []() { 
        Serial.println("Watchdog timeout - restarting");
        ESP.restart();
    });

    server.begin();
    Serial.println("HTTP server started");
    Serial.printf("Free heap after setup: %d bytes\n", ESP.getFreeHeap());
    Serial.println("Socket test skipped during initialization");
}

void loop() {
    server.handleClient(); 
    static unsigned long lastReadingTime = 0;
    if (millis() - lastReadingTime > 5000) { 
        updatePowerReadings();
        lastReadingTime = millis();
        yield(); 
    }
    yield(); 
    static unsigned long lastWatchdogReset = 0;
    if (millis() - lastWatchdogReset > 30000) {
        watchdogTicker.detach();
        watchdogTicker.attach(60, []() { 
            Serial.println("Watchdog timeout - restarting");
            ESP.restart();
        });
        lastWatchdogReset = millis();
    }
    yield();
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command.length() > 0) {
            processSerialCommand(command);
        }
    }
    yield(); 
    static unsigned long lastDisplayTime = 0;
    if (millis() - lastDisplayTime > 60000) { 
        Serial.println("\n==================================");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("DEVICE IP: " + WiFi.localIP().toString());
        } else {
            Serial.println("DEVICE IP (AP): " + WiFi.softAPIP().toString());
        }
        printStatus();
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.println("==================================");
        lastDisplayTime = millis();
        Serial.println("Sensor Readings:");
        Serial.print("  Sensor 1 (D3): ");
        Serial.println(readSensor(s1_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 2 (D4): ");
        Serial.println(readSensor(s2_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 3 (D8): ");
        Serial.println(readSensor(s3_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 4 (D7): ");
        Serial.println(readSensor(s4_pin) ? "ACTIVE" : "INACTIVE");
        Serial.printf("Free heap after loop: %d bytes\n", ESP.getFreeHeap()); // Check memory
    }
    yield(); 
    yield();   
}

bool readSensor(int pin) {
    return (digitalRead(pin) == LOW); 
}

void addLog(const String& message) {
    if (ESP.getFreeHeap() < 4000) {
        for (int i = 0; i < MAX_LOGS; i++) {
            requestLogs[i] = "";
        }
        logIndex = 0;
        Serial.println("Memory low! Logs cleared.");
    }

    String timestamp = "[" + String(millis() / 1000) + "]";
    requestLogs[logIndex] = timestamp + " " + message; 
    logIndex = (logIndex + 1) % MAX_LOGS;
    Serial.println(message);
}

void setupRoutes() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/socket", HTTP_GET, handleSocketCommand);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/power", HTTP_GET, handlePowerStatus);
    server.on("/test", HTTP_GET, handleTest);
    server.on("/reset", HTTP_GET, handleReset);
    server.on("/ip", HTTP_GET, []() {
        server.send(200, "text/plain", WiFi.status() == WL_CONNECTED ?
                   WiFi.localIP().toString() : WiFi.softAPIP().toString());
    });
    server.onNotFound([]() {
        String uri = server.uri();
        addLog("NOT FOUND: " + uri); 
        server.send(404, "text/plain", "Not found");
    });
    server.enableCORS(true);
}

void handleRoot() {
    String html = "<html><head><title>ESP Controller</title>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<style>body{font-family:sans-serif;margin:10px} "
                  ".h{background:#2196F3;color:white;padding:10px} "
                  ".s{padding:10px;margin:5px 0;border:1px solid #ddd} "
                  ".b{padding:5px 10px;margin:5px;color:white;text-decoration:none;display:inline-block} "
                  ".on{background:#4CAF50} .off{background:#f44336} "
                  ".a{background:#FF9800}</style></head><body>"
                  "<div class='h'><h2>Socket Controller</h2>"
                  "<p>IP: " + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "</p></div>";

    for (int i = 1; i <= 4; i++) {
        bool state = false;
        int power = 0;

        switch (i) {
            case 1: state = socket1State; power = socket1Power; break;
            case 2: state = socket2State; power = socket2Power; break;
            case 3: state = socket3State; power = socket3Power; break;
            case 4: state = socket4State; power = socket4Power; break;
        }

        html += "<div class='s'><h3>Socket " + String(i) + "</h3>"
                "<p>Status: <b>" + String(state ? "ON" : "OFF") + "</b> | Power: <b>" + String(power) + " W</b></p>"
                "<a href='/socket?id=" + String(i) + "&state=on' class='b on'>ON</a>"
                "<a href='/socket?id=" + String(i) + "&state=off' class='b off'>OFF</a></div>";
    }

    html += "<div style='margin-top:15px'>"
            "<a href='/test' class='b a'>Test</a> "
            "<a href='/reset' class='b a'>Reset</a> "
            "<a href='/' class='b a'>Refresh</a></div>"
            "</body></html>";

    server.send(200, "text/html", html);
    yield(); 
}

void updatePowerReadings() {
    if (socket1State) socket1Power = random(80, 120);
    else socket1Power = 0;
    if (socket2State) socket2Power = random(150, 200);
    else socket2Power = 0;
    if (socket3State) socket3Power = random(50, 75);
    else socket3Power = 0;
    if (socket4State) socket4Power = random(200, 250);
    else socket4Power = 0;
    yield();
}

void printStatus() {
    Serial.println("=== Socket Status ===");
    Serial.printf("Socket 1: %s\n", socket1State ? "ON" : "OFF");
    Serial.printf("Socket 2: %s\n", socket2State ? "ON" : "OFF");
    Serial.printf("Socket 3: %s\n", socket3State ? "ON" : "OFF");
    Serial.printf("Socket 4: %s\n", socket4State ? "ON" : "OFF");
    Serial.println("====================");
}

void controlSocket(int id, bool state) {
    if (id < 1 || id > 4) {
        Serial.println("Error: Invalid socket ID. Must be between 1 and 4.");
        return;
    }

    Serial.printf("Controlling socket %d -> %s\n", id, state ? "ON" : "OFF");
    int pinValue = state ? SOCKET_ON : SOCKET_OFF;

    switch (id) {
        case 1: digitalWrite(relay1, pinValue); socket1State = state; break;
        case 2: digitalWrite(relay2, pinValue); socket2State = state; break;
        case 3: digitalWrite(relay3, pinValue); socket3State = state; break;
        case 4: digitalWrite(relay4, pinValue); socket4State = state; break;
        default: Serial.println("Error: Invalid socket ID."); return;
    }
    printStatus();
    yield();
}

void processSerialCommand(const String& command) {
    Serial.printf("Processing command: %s\n", command.c_str());
    if (command.length() == 0 || command == "status") {
        printStatus();
        return;
    }

    int socketId = 0;
    bool state = true;
    int commaIndex = command.indexOf(',');

    if (commaIndex == -1) {
        socketId = command.toInt();
    } else {
        socketId = command.substring(0, commaIndex).toInt();
        String stateStr = command.substring(commaIndex + 1);
        state = (stateStr == "true" || stateStr == "1" || stateStr == "on");
    }
    controlSocket(socketId, state);
    yield();
}

void handleSocketCommand() {
    String idArg = server.arg("id");
    String stateArg = server.arg("state");

    addLog("Socket: id=" + idArg + "&state=" + stateArg);

    if (idArg.length() == 0) {
        server.send(400, "text/plain", "Missing socket ID");
        return;
    }

    int socketId = idArg.toInt();
    bool state = true;

    if (stateArg.length() > 0) {
        String stateStr = stateArg;
        stateStr.toLowerCase();
        state = (stateStr == "true" || stateStr == "1" || stateStr == "on");
    }

    if (socketId < 1 || socketId > 4) {
        server.send(400, "text/plain", "Invalid socket ID. Must be 1-4");
        return;
    }

    controlSocket(socketId, state);
    String response = "{\"success\":true,\"socketId\":" + String(socketId) + ",\"state\":\"" + (state ? "on" : "off") + "\",\"power\":" + String(getPowerForSocket(socketId)) + "}";
    server.send(200, "application/json", response);
    yield();
}

int getPowerForSocket(int socketId) {
    switch (socketId) {
        case 1: return socket1Power;
        case 2: return socket2Power;
        case 3: return socket3Power;
        case 4: return 0;
        default: return 0;
    }
}

void handleStatus() {
    addLog("Status request");
    String status = "{\"socket1\":" + String(socket1State ? "true" : "false") +
                    ",\"socket2\":" + String(socket2State ? "true" : "false") +
                    ",\"socket3\":" + String(socket3State ? "true" : "false") +
                    ",\"socket4\":" + String(socket4State ? "true" : "false") +
                    ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"}";
    server.send(200, "application/json", status);
    yield();
}

void handlePowerStatus() {
    addLog("Power status");
    String status = "{\"socket1\":{\"state\":" + String(socket1State ? "true" : "false") + ",\"power\":" + String(socket1Power) + "},"
                    "\"socket2\":{\"state\":" + String(socket2State ? "true" : "false") + ",\"power\":" + String(socket2Power) + "},"
                    "\"socket3\":{\"state\":" + String(socket3State ? "true" : "false") + ",\"power\":" + String(socket3Power) + "},"
                    "\"socket4\":{\"state\":" + String(socket4State ? "true" : "false") + ",\"power\":" + String(socket4Power) + "}}";
    server.send(200, "application/json", status);
    yield();
}

void testSockets(bool quickTest) {
    int onTime = quickTest ? 200 : 1000;
    int offTime = quickTest ? 100 : 500;

    addLog("Testing sockets");
    Serial.println("Testing all sockets in sequence...");

    for (int i = 1; i <= 4; i++) {
        Serial.printf("Testing Socket %d...\n", i);
        controlSocket(i, true);
        delay(onTime);  
        controlSocket(i, false);
        delay(offTime); 
        yield();      
    }
    Serial.println("Socket test complete");
    yield();
}

void handleTest() {
    addLog("Test requested");
    server.send(200, "text/plain", "Socket test started");
    testSockets(false);
    printStatus();
    yield();
}

void handleReset() {
    addLog("Reset requested");
    server.send(200, "text/plain", "ESP8266 will restart in 2 seconds");
    delay(1000); 
    Serial.println("Resetting device...");
    delay(1000); 
    ESP.restart();
    yield();
}