#include <WiFi.h>
#include <WebServer.h>
#include <Ticker.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

const char* ssid = "PLDTHOMEFIBR16ce0";
const char* password = "PLDTWIFI2vgcp";

#define SOCKET_ON LOW
#define SOCKET_OFF HIGH

WebServer server(80);
Ticker watchdogTicker;
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int relay1 = 5;
const int relay2 = 15;
const int relay3 = 19;
const int relay4 = 4;

const int sc_pin = 16;
const int s1_pin = 23;
const int s2_pin = 25;
const int s3_pin = 26;
const int s4_pin = 17;

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

unsigned long timerDuration = 0;
unsigned long timerStartTime = 0;
bool timerRunning = false;
int timerSocketId = 0; 

String timerDeviceId = "";  
int timerRelay = 0;        

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

void handleSetTimer();
void updateLCD();
String formatTime(unsigned long milliseconds);
int getActiveSocket();

void setup() {
    delay(2000);
    Serial.begin(115200);
    Serial.flush();
    delay(100);

    Serial.println();
    Serial.println("============================");
    Serial.println("ESP32 SOCKET CONTROLLER");
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
    pinMode(s3_pin, INPUT_PULLUP);
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

        // ✅ Fixed: Use MAC address instead of getChipId
        uint64_t chipId = ESP.getEfuseMac();  // 64-bit MAC address
        String apName = "ESP_Socket_" + String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX);
        apName.toUpperCase();

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

    Wire.begin(21, 22);  
    lcd.init();     
    lcd.backlight(); 
    lcd.clear();
    lcd.print("Initializing...");

    setupRoutes(); 

    watchdogTicker.attach(60, []() {
        Serial.println("Watchdog timeout - restarting");
        ESP.restart();
    });

    server.begin();
    Serial.println("HTTP server started");
    Serial.printf("Free heap after setup: %d bytes\n", ESP.getFreeHeap());
    Serial.println("Socket test skipped during initialization");

    lcd.clear();
    lcd.print("Ready");
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
        Serial.print("  Sensor 1 (GPIO23): ");
        Serial.println(readSensor(s1_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 2 (GPIO22): ");
        Serial.println(readSensor(s2_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 3 (GPIO2): ");
        Serial.println(readSensor(s3_pin) ? "ACTIVE" : "INACTIVE");
        Serial.print("  Sensor 4 (GPIO4): ");
        Serial.println(readSensor(s4_pin) ? "ACTIVE" : "INACTIVE");
        Serial.printf("Free heap after loop: %d bytes\n", ESP.getFreeHeap()); // Check memory
    }
    yield();
    if (timerRunning) {
        unsigned long elapsedTime = millis() - timerStartTime;
        if (elapsedTime >= timerDuration) {
            timerRunning = false;
            Serial.println("Timer expired!");
            controlSocket(timerRelay, false); 
            Serial.printf("Socket %d turned OFF due to timer expiration\n", timerRelay);
            timerSocketId = 0;
            timerDeviceId = "";
            timerRelay = 0;
            lcd.clear();
            lcd.print("Timer Expired!");
            delay(2000);
            lcd.clear();
        }
        updateLCD();
    }
    yield();
    updateLCD();

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
    server.on("/setTimer", HTTP_GET, handleSetTimer); 
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
                  ".a{background:#FF9800} "
                  "input{width:50px;padding:5px}</style></head><body>"
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
                "<a href='/socket?id=" + String(i) + "&state=off' class='b off'>OFF</a>"
                "<form style='display:inline-block' action='/socket' method='get'>"
                "<input type='hidden' name='id' value='" + String(i) + "'>"
                "<input type='hidden' name='state' value='on'>"
                "<input type='number' name='minutes' min='1' placeholder='Min'>"
                "<input type='text' name='applianceName' placeholder='Name'>"
                "<button type='submit' class='b a'>Timer</button>"
                "</form></div>";
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
    String minutesArg = server.arg("minutes");
    String applianceNameArg = server.arg("applianceName");
    addLog("Socket: id=" + idArg + "&state=" + stateArg + "&minutes=" + minutesArg);
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
    if (minutesArg.length() > 0 && state) {  
        int minutes = minutesArg.toInt();
        if (minutes > 0) {
          
            timerDuration = static_cast<unsigned long>(minutes) * 60 * 1000;
            timerStartTime = millis();
            timerRunning = true;
            timerSocketId = socketId;
            timerDeviceId = String(socketId);
            timerRelay = socketId;
            lcd.clear();
            lcd.setCursor(0, 0);
            String displayName = applianceNameArg.length() > 0 ? applianceNameArg : "Socket " + String(socketId);
            if (displayName.length() > 16) {
                displayName = displayName.substring(0, 13) + "...";
            }
            lcd.print(displayName);
            
            lcd.setCursor(0, 1);
            lcd.print(String(minutes) + " min timer");
            
            addLog("Timer set for " + displayName + ": " + String(minutes) + " minutes");
        }
    }

    String response = "{\"success\":true,\"socketId\":" + String(socketId) + 
                     ",\"state\":\"" + (state ? "on" : "off") + "\"" +
                     ",\"power\":" + String(getPowerForSocket(socketId)) + 
                     (minutesArg.length() > 0 ? ",\"timerMinutes\":" + minutesArg : "") + "}";
    server.send(200, "application/json", response);
    yield();
}

int getPowerForSocket(int socketId) {
    switch (socketId) {
        case 1: return socket1Power;
        case 2: return socket2Power;
        case 3: return socket3Power;
        case 4: return socket4Power;
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
    server.send(200, "text/plain", "ESP32 will restart in 2 seconds");
    delay(1000);
    Serial.println("Resetting device...");
    delay(1000);
    ESP.restart();
    yield();
}

void handleSetTimer() {
    String minutesStr = server.arg("minutes");
    String deviceIdStr = server.arg("deviceId");
    String relayStr = server.arg("relay");
    String applianceNameStr = server.arg("applianceName"); // Add appliance name parameter

    if (minutesStr.isEmpty() || deviceIdStr.isEmpty() || relayStr.isEmpty()) {
        server.send(400, "text/plain", "Missing parameters: minutes, deviceId, or relay");
        return;
    }

    int minutes = minutesStr.toInt();
    int relay = relayStr.toInt();
    String deviceId = deviceIdStr;
    String applianceName = applianceNameStr; // Store appliance name

    if (minutes <= 0 || relay < 1 || relay > 4) {
        server.send(400, "text/plain", "Invalid parameters");
        return;
    }

    // Reset and start new timer
    timerDuration = static_cast<unsigned long>(minutes) * 60 * 1000; // Convert to milliseconds
    timerStartTime = millis();
    timerRunning = true;
    timerSocketId = relay;
    timerDeviceId = deviceId;
    timerRelay = relay;
    controlSocket(relay, true);
    String logMessage = "Timer set: " + applianceName + " | " + String(minutes) + " min";
    addLog(logMessage);
    

    lcd.clear();
    lcd.setCursor(0, 0);
    if (applianceName.length() > 16) {
        applianceName = applianceName.substring(0, 13) + "...";
    }
    lcd.print(applianceName);
    String timeStr = String(minutes) + " min timer";
    lcd.setCursor(0, 1);
    lcd.print(timeStr);

    server.send(200, "application/json", "{\"success\":true,\"message\":\"Timer set for " + 
                applianceName + "\",\"duration\":" + String(minutes) + "}");
    yield();
}

void updateLCD() {
    if (!timerRunning) {
        displayDefaultScreen();
        return;
    }

    unsigned long remainingTime = timerDuration - (millis() - timerStartTime);
    if (remainingTime > 0) {
        lcd.clear();
        lcd.setCursor(0, 0);
        String displayName = timerDeviceId.length() > 0 ? timerDeviceId : "Socket " + String(timerRelay);
        if (displayName.length() > 16) {
            displayName = displayName.substring(0, 13) + "...";
        }
        lcd.print(displayName);
        
        lcd.setCursor(0, 1);
        String timeStr = formatTime(remainingTime);
        int padding = (16 - timeStr.length()) / 2;  
        for (int i = 0; i < padding; i++) {
            lcd.print(" ");
        }
        lcd.print(timeStr);
    } else {
        timerExpired();
    }
}
void displayDefaultScreen() {
    lcd.clear();
    lcd.setCursor(0, 0);
    int activeSocket = getActiveSocket();
    if (activeSocket > 0) {
        lcd.print("Socket ");
        lcd.print(activeSocket);
        lcd.print(" is ON");
    } else {
        lcd.print("All Sockets OFF");
    }
    lcd.setCursor(0, 1);
    lcd.print("Ready");
}

void timerExpired() {
    timerRunning = false;
    controlSocket(timerRelay, false); 
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Timer Expired!");
    lcd.setCursor(0, 1);
    lcd.print("Socket ");
    lcd.print(timerRelay);
    lcd.print(" OFF");
    delay(2000); 
    displayDefaultScreen();
}

String formatTime(unsigned long milliseconds) {
    unsigned long totalSeconds = milliseconds / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;

    char timeStr[9];
    sprintf(timeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(timeStr);
}


int getActiveSocket() {
    if (socket1State) return 1;
    if (socket2State) return 2;
    if (socket3State) return 3;
    if (socket4State) return 4;
    return 0;  
}

