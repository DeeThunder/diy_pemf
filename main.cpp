#include <Arduino.h>

#include <SoftwareSerial.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WebServer.h>




#define OLED_SDA 6
#define OLED_SCL 7
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

const char *apSSID = "PEMF Wireless";
const char *apPassword = "12345678";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);


/**
 * Values for wound healing:
 * Wound healing	15 minutes	1-5 Hz
 * Pain associated with wound healing	15 minutes	11-15 or 17 Hz
 * Bruises	15 minutes	10 Hz
 * Phantom pain	15 minutes	16-19 Hz
 * Bruises	16 minutes	14 Hz
 */
const int defaultFreq = 5;  // Hz
const int defaultDuty = 10;   // %
const int defaultTime = 10;    // minutes



struct SignalData {
  float frequency;
  float dutyCycle;
};

#define RELAY_SERIAL_RX 20
#define RELAY_SERIAL_TX 21
#define RELAY_TIMER_SERIAL Serial1



struct RelayData {
  int onTime;   // e.g. OP time in seconds
  int offTime;  // e.g. CL time in seconds
};

#define SIGNAL_RX_PIN 0
#define SIGNAL_TX_PIN 1
#define DEFAULT_OFF_TIME_SECS 3
SoftwareSerial signalSerial(SIGNAL_RX_PIN, SIGNAL_TX_PIN); 






void setupSerial() {
  // Configure SoftwareSerial for signal generator
  signalSerial.begin(9600);
  // Configure Relay Timer serial (UART1) at 9600, map to pins 20/21
  RELAY_TIMER_SERIAL.begin(9600, SERIAL_8N1, RELAY_SERIAL_RX, RELAY_SERIAL_TX);
  Serial.begin(115200);
}

// Read current values in signal generator
SignalData readSignalGenerator() {
  SignalData data = {0, 0};
  signalSerial.write("read"); // Send read command
  delay(100); // Wait for response
  if (signalSerial.available()) {
    String resp = signalSerial.readStringUntil('\n');

    int idxHz = resp.indexOf("Hz");
    int idxPct = resp.indexOf("%");
    /**
     * ! The "D=  " and "F=" is intentionally given or not given a space. Don't
     * ! edit; its important to the format
     */
    int idxF = resp.indexOf("F=");
    int idxD = resp.indexOf("D= ");

    if (idxHz > 0 && idxF >= 0) {
      // Extract number between F= and Hz
      String fstr = resp.substring(idxF+2, idxHz);
      data.frequency = fstr.toFloat();
    }
    if (idxPct > 0 && idxD >= 0) {
      // Extract number between D= and %
      String dstr = resp.substring(idxD+2, idxPct);
      data.dutyCycle = dstr.toFloat();
    }
  }
  return data;
}

// Return command string "F..." for given frequency (Hz)
String formatFrequencyCommand(float freqHz) {
  if (freqHz <= 999) {
    // Format as three-digit Hz (e.g., 005, 101)
    int f = (int)freqHz;
    char buf[5];
    sprintf(buf, "F%03d", f);
    return String(buf);
  } else if (freqHz < 10000) {
    // Format as kHz with two decimals (e.g., 1000Hz -> "F1.00")
    char buf[10];
    sprintf(buf, "F%.2f", freqHz / 1000.0);
    return String(buf);
  } else if (freqHz < 100000) {
    // Format as kHz with one decimal (e.g., 10500Hz -> "F10.5")
    char buf[10];
    sprintf(buf, "F%.1f", freqHz / 1000.0);
    return String(buf);
  } else {
    // Format 100–150 kHz as X.X.X (e.g., 105000Hz -> "F1.0.5")
    int khz = (int)(freqHz / 1000);
    int hundreds = khz / 100;
    int tens = (khz / 10) % 10;
    int ones = khz % 10;
    char buf[10];
    sprintf(buf, "F%d.%d.%d", hundreds, tens, ones);
    return String(buf);
  }
}

// Return command string "Dxxx" for given duty percent
String formatDutyCommand(int dutyPercent) {
  if (dutyPercent < 0) dutyPercent = 0;
  if (dutyPercent > 100) dutyPercent = 100;
  char buf[5];
  sprintf(buf, "D%03d", dutyPercent);
  return String(buf);
}






// Return command string "OP:xxxx" for given time in minutes
String formatTimingCommand(int timeInMins) {
  int seconds = timeInMins * 60;
  if (seconds < 1) seconds = 1;
  if (seconds > 9999) seconds = 9999;

  // Format as OP:xxxx with leading zeros
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "OP:%04d", seconds);
  
  return String(buffer);
}

// Alternative command string "OPxxxx"
String formatTimingCommand2(int timeInMins) {
  int seconds = timeInMins * 60;
  if (seconds < 1) seconds = 1;
  if (seconds > 9999) seconds = 9999;

  // Format as OP:xxxx with leading zeros
  char buffer[10];
  snprintf(buffer, sizeof(buffer), "OP:%04d", seconds);
  
  return String(buffer);
}




// sets the modules to the default variables
void setupModules() {
  signalSerial.write(formatFrequencyCommand(defaultFreq).c_str());
  signalSerial.write(formatDutyCommand(defaultDuty).c_str());

  RELAY_TIMER_SERIAL.write(formatTimingCommand(defaultTime).c_str());
  RELAY_TIMER_SERIAL.write(formatTimingCommand2(defaultTime).c_str());

  // change to appropriate mode to set values
  RELAY_TIMER_SERIAL.write("P5");
  RELAY_TIMER_SERIAL.write("LP:0001");
  RELAY_TIMER_SERIAL.write("LP0001");

  RELAY_TIMER_SERIAL.write("CL:0003");
  RELAY_TIMER_SERIAL.write("CL0003");
}




// Turns on the OLED screen
void setupOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);  // Initialize I²C on custom pins
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 init failed")); 
    while (true); // Stop if OLED init fails:contentReference[oaicite:1]{index=1}.
  }
  display.clearDisplay();
  display.setTextSize(1);          // Larger text for title
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("PEMF Machine"); // Startup message
  display.display();
}

// Sets up Wifi to enable wireless mode
void setupWiFiAP() {
  Serial.println("Configuring Wi-Fi AP...");
  // Start Wi-Fi in AP mode with maximum of one connection
  if (!WiFi.softAP(apSSID, apPassword, 1, false, 1)) {
    Serial.println("AP creation failed");
    display.setCursor(0, 16);
    display.setTextSize(1);
    display.println("AP failed");
    display.display();
    return;
  }
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(apIP);
  display.setCursor(0, 16);               // Next line on OLED
  display.setTextSize(1);
  display.printf("IP: %s", apIP.toString().c_str());
  display.display();                      // Show IP on OLED
  Serial.println("Web server started");
}



float currentFreq = defaultFreq;
float currentDuty = defaultDuty;
int currentTime = defaultTime;

// Creates web server dashboard
void setupWebServer() {

  server.on("/", HTTP_GET, []() {
    display.setCursor(0, 26);
    display.println("Client connected");
    display.display();

    // Build HTML form with current values
    String html = R"rawliteral(
      <html>
        <head><title>PEMF Settings</title></head>
        <body>
          <h1>PEMF Machine Settings</h1>
          <form action="/" method="POST">
            Frequency (Hz) [Max: 150,000Hz]: <input type="number" name="freq" value=")rawliteral";
    html += String(currentFreq);
    html += R"rawliteral("><br>
            Duty Cycle (%) [Max: 100%]: <input type="number" name="duty" value=")rawliteral";
    html += String(currentDuty);
    html += R"rawliteral("><br>
            Session Time (min) [Max: 120 mins]: <input type="number" name="time" value=")rawliteral";
    html += String(currentTime);
    html += R"rawliteral("><br>
            <input type="submit" value="Start">
          </form>
          <p style="color:red">Avoid duty values higher than 10% to prevent coil from overheating</p>
          <p>You can find guidelines for values at <a href="https://www.pemfsupply.com/pages/frequency">here</a></p>
        </body>
      </html>
    )rawliteral";

    server.send(200, "text/html", html);
  });

  server.on("/", HTTP_POST, []() {
    // Parse and update values
    if (server.hasArg("freq")) {
      currentFreq = server.arg("freq").toFloat();
    }
    if (server.hasArg("duty")) {
      currentDuty = server.arg("duty").toFloat();
    }
    if (server.hasArg("time")) {
      currentTime = server.arg("time").toInt();
    }

    // Send formatted commands to your devices (replace with actual transmission logic)
    signalSerial.write(formatFrequencyCommand(currentFreq).c_str());
    signalSerial.write(formatDutyCommand(currentDuty).c_str());
    RELAY_TIMER_SERIAL.write(formatTimingCommand(currentTime).c_str());
    RELAY_TIMER_SERIAL.write(formatTimingCommand2(currentTime).c_str());
    RELAY_TIMER_SERIAL.write("P6");

    // Redirect back to GET page (refresh with updated values)
    server.sendHeader("Location", "/");
    server.send(303);  // HTTP 303: See Other (used for redirects after POST)
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}









void setup() {
  setupSerial();
  setupModules();

  setupOLED();
  setupWiFiAP();

  setupWebServer();
}

void loop() {
  server.handleClient(); // Must be called regularly:contentReference[oaicite:7]{index=7}
}
