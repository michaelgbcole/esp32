#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2

#define NYAN_WIDTH 24
#define NYAN_HEIGHT 16
#define TRAIL_LENGTH 25

#define ST77XX_PINK 0xFCBF
#define ST77XX_GRAY 0x8410
#define ST77XX_ORANGE 0xFD20

struct Position {
   float x;
   float y;
};

Position trailPositions[TRAIL_LENGTH];
float catY = 100;
float yVelocity = 0.1;
bool movingUp = true;


// Colors for rainbow trail
uint16_t rainbowColors[] = {
  ST77XX_BLACK,
   ST77XX_RED,
   ST77XX_ORANGE, 
   ST77XX_YELLOW,
   ST77XX_GREEN,
   ST77XX_BLUE,
   ST77XX_MAGENTA,
   ST77XX_BLACK
};

const char* html = R"(
<!DOCTYPE html>
<html>
<body>
  <h1>Hello from ESP32!</h1>
</body>
</html>
)";

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

const char* ssid = "Jimmy";
const char* password = "710Barneson";

WebServer server(80);

// Stats variables
unsigned long bytesTransferred = 0;
unsigned long previousBytes = 0;
unsigned long previousTime = 0;
unsigned long startTime = 0;
int activeConnections = 0;
float bytesPerSec = 0;

// For tracking clients
#define MAX_CLIENTS 20
IPAddress clientIPs[MAX_CLIENTS];
unsigned long clientLastSeen[MAX_CLIENTS];

String getUptime() {
    unsigned long currentMillis = millis();
    unsigned long seconds = currentMillis / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    char uptime[9];
    sprintf(uptime, "%02lu:%02lu:%02lu", hours, minutes % 60, seconds % 60);
    return String(uptime);
}

float getTemp() {
    return (float)(temprature_sens_read());
}


void drawNyanCat() {
    int baseX = 80;  // Cat's x position
    static const float MIN_Y = 95;  // Minimum Y position of cat
    static const float MAX_Y = 105; // Maximum Y position of cat
    
    // Update cat position
    if(movingUp) {
        catY -= yVelocity;
        if(catY < MIN_Y) movingUp = false;
    } else {
        catY += yVelocity;
        if(catY > MAX_Y) movingUp = true;
    }
    
    // Calculate the exact regions to clear based on cat size and movement range
    float topClearY = MIN_Y - (NYAN_HEIGHT/2) - 1;     // Start clearing just above highest point
    float bottomClearY = MAX_Y + (NYAN_HEIGHT/2) + 1;  // Start clearing just below lowest point
    
    // Clear the space just above the highest point the cat reaches
    
    // Shift trail positions
    for(int i = TRAIL_LENGTH-1; i > 0; i--) {
        trailPositions[i] = trailPositions[i-1];
    }
    trailPositions[0] = {(float)baseX - NYAN_WIDTH/2 - 5, catY};
    
    // Draw rainbow trail
    int trailStartX = baseX - NYAN_WIDTH/2 - 5;
    for(int i = 0; i < TRAIL_LENGTH; i++) {
        int segmentX = trailStartX - (i * 3);
        if(segmentX > 0) {
            for(int stripe = 0; stripe < 8; stripe++) {
                tft.fillRect(segmentX,
                            trailPositions[i].y - 8 + (stripe * 2),
                            3,  // Width of segment
                            2,  // Height of stripe
                            rainbowColors[stripe]);
            }
        }
    }
    
    // Draw Pop-tart body
    tft.fillRect(baseX - NYAN_WIDTH/2, 
                 catY - NYAN_HEIGHT/2, 
                 NYAN_WIDTH, 
                 NYAN_HEIGHT, 
                 ST77XX_PINK);
    
    // Add dark pink outline
    tft.drawRect(baseX - NYAN_WIDTH/2, 
                 catY - NYAN_HEIGHT/2, 
                 NYAN_WIDTH, 
                 NYAN_HEIGHT, 
                 ST77XX_MAGENTA);

    tft.drawRect(baseX - NYAN_WIDTH/2, 
                 catY - NYAN_HEIGHT/2, 
                 NYAN_WIDTH+1, 
                 NYAN_HEIGHT+1, 
                 ST77XX_BLACK);
    
    // Draw cat face (gray rectangle)
    tft.fillRect(baseX + 2, catY - 3, 6, 6, ST77XX_GRAY);
    
    // Eyes
    tft.drawPixel(baseX + 3, catY - 1, ST77XX_BLACK);
    tft.drawPixel(baseX + 6, catY - 1, ST77XX_BLACK);
    
    // Mouth
    tft.drawPixel(baseX + 4, catY + 1, ST77XX_BLACK);
    tft.drawPixel(baseX + 5, catY + 1, ST77XX_BLACK);
}
// Draw static labels once
void drawLabels() {
    tft.setTextSize(1);
    
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(2, 5);
    tft.print("USR:");
    
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(2, 20);
    tft.print("B/s:");
    
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(2, 35);
    tft.print("MEM:");
    
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(2, 50);
    tft.print("TMP:");
    
    tft.setTextColor(ST77XX_MAGENTA);
    tft.setCursor(2, 65);
    tft.print("UP:");
}

// Update only the values, not the labels
void updateValues() {
    static String oldUptime;
    static int oldConnections = -1;
    static long oldMem = -1;
    static float oldTemp = -999;
    static float oldBytes = -1;
    
    String newUptime = getUptime();
    int newMem = ESP.getFreeHeap() / 1024;
    float newTemp = getTemp();
    
    // Only update each value if it has changed
    if (oldConnections != activeConnections) {
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
        tft.setCursor(45, 5);
        tft.print("    ");  // Clear old value
        tft.setCursor(45, 5);
        tft.print(activeConnections);
        oldConnections = activeConnections;
    }
    
    if (oldBytes != bytesPerSec) {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
        tft.setCursor(45, 20);
        tft.print("        ");  // Clear old value
        tft.setCursor(45, 20);
        tft.print(bytesPerSec, 0);
        oldBytes = bytesPerSec;
    }
    
    if (oldMem != newMem) {
        tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
        tft.setCursor(45, 35);
        tft.print("        ");  // Clear old value
        tft.setCursor(45, 35);
        tft.print(520 - newMem);
        tft.print("K Used");
        oldMem = newMem;
    }
    
    if (oldTemp != newTemp) {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.setCursor(45, 50);
        tft.print("        ");  // Clear old value
        tft.setCursor(45, 50);
        tft.print(newTemp, 1);
        tft.print("\xF7""F");
        oldTemp = newTemp;
    }
    
    if (oldUptime != newUptime) {
        tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
        tft.setCursor(45, 65);
        tft.print("        ");  // Clear old value
        tft.setCursor(45, 65);
        tft.print(newUptime);
        oldUptime = newUptime;
    }
}

void updateClientList() {
    IPAddress clientIP = server.client().remoteIP();
    unsigned long currentTime = millis();
    
    bool found = false;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clientIPs[i] == clientIP) {
            clientLastSeen[i] = currentTime;
            found = true;
            break;
        }
        if(clientIPs[i] == IPAddress(0,0,0,0)) {
            clientIPs[i] = clientIP;
            clientLastSeen[i] = currentTime;
            found = true;
            break;
        }
    }

    activeConnections = 0;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clientIPs[i] != IPAddress(0,0,0,0)) {
            if(currentTime - clientLastSeen[i] > 10000) {
                clientIPs[i] = IPAddress(0,0,0,0);
                clientLastSeen[i] = 0;
            } else {
                activeConnections++;
            }
        }
    }
}

void handleRoot() {
    updateClientList();
    bytesTransferred += strlen(html);    
    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    startTime = millis();
    
    tft.initR(INITR_GREENTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);  // Only black screen once at startup
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(5, 20);
    tft.print("Connecting to WiFi...");
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting...");
    }
    
    // Show IP briefly
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(5, 20);
    tft.print("IP: ");
    tft.println(WiFi.localIP());
    delay(2000);
    
    // Initialize client tracking
    for(int i = 0; i < MAX_CLIENTS; i++) {
        clientIPs[i] = IPAddress(0,0,0,0);
        clientLastSeen[i] = 0;
    }

    for(int i = 0; i < TRAIL_LENGTH; i++) {
       trailPositions[i] = {80.0f, 100.0f};
   }

    
    tft.fillScreen(ST77XX_BLACK);  // Final clear before main display
    drawLabels();  // Draw static labels once
    
    server.on("/", HTTP_GET, handleRoot);
    server.begin();
    
    previousTime = millis();
}

void loop() {
    server.handleClient();
    
    // Update stats
    unsigned long currentTime = millis();
    unsigned long deltaTime = currentTime - previousTime;
    float deltaBytes = bytesTransferred - previousBytes;
    bytesPerSec = (deltaBytes * (1000.0 / deltaTime));
    
    previousBytes = bytesTransferred;
    previousTime = currentTime;
    
    updateValues();  // Update the stats values
    drawNyanCat();   // Just draw cat without clearing
    delay(16);       // Small delay for smooth animation
}
