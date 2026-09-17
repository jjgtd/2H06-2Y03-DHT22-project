// ==================================================================================== //
// PROJECT:        Low-Power Battery-Operated Environmental Monitor Firmware            //
// ARCHITECTURE:   ESP32-C3 Super Mini Microcontroller Board                            //
// SENSOR:         DHT22 / AM2302 High-Precision Climate Telemetry Module               //
// DISPLAY:        SSD1306 0.96-inch Monochrome OLED Display (128x64 Pixel Matrix)      //
// PROTOCOL:       I2C (Inter-Integrated Circuit) Hardware Communications Bus           //
// POWER MODE:     Battery / USB-C Ultra-Low-Power Deep Sleep Duty Cycle                //
// VERSION:        3.6.0 - Title Case Labels & Standard Typography Scaling              //
// ==================================================================================== //

#include <Wire.h>            // Architecture I2C bus communication driver library
#include <Adafruit_GFX.h>    // Adafruit graphics primitive rendering engine
#include <Adafruit_SSD1306.h>// Core SSD1306 OLED hardware display controller library
#include "DHT.h"             // Sensor communication library for DHT22 climate module

// ==================================================================================== //
// SECTION 1: HARDWARE PINOUT ASSIGNMENTS AND CONFIGURATION CONSTANTS                   //
// ==================================================================================== //
#define DHTPIN              4        // Dedicated GPIO pin 4 mapped to DHT22 single-wire data pin
#define DHTTYPE             DHT22    // Hardware sensor variant specified as DHT22 (AM2302)
#define SDA_PIN             9        // Dedicated GPIO pin 9 configured as I2C Serial Data (SDA)
#define SCL_PIN             8        // Dedicated GPIO pin 8 configured as I2C Serial Clock (SCL)

#define SCREEN_WIDTH        128      // Physical width of SSD1306 OLED display in pixels
#define SCREEN_HEIGHT       64       // Physical height of SSD1306 OLED display in pixels
#define OLED_RESET          -1       // Reset pin assignment (-1 indicates shared MCU reset)
#define OLED_I2C_ADDRESS    0x3C     // Primary 7-bit I2C bus address for SSD1306 controller

#define uS_TO_S_FACTOR      1000000ULL // Multiplier constant to convert seconds into microseconds
#define SLEEP_INTERVAL_SEC  50       // Total time duration in seconds assigned for deep sleep cycle

#define COMFORT_TEMP_MIN    18.0f    // Lower temperature boundary (°C) for ambient comfort index
#define COMFORT_TEMP_MAX    27.0f    // Upper temperature boundary (°C) for ambient comfort index
#define COMFORT_HUM_MIN     30.0f    // Lower relative humidity boundary (%) for ambient comfort
#define COMFORT_HUM_MAX     75.0f    // Upper relative humidity boundary (%) for ambient comfort

// ==================================================================================== //
// SECTION 2: GRAPHICAL BITMAP ASSETS (FLASH MEMORY PROGMEM STORAGE)                    //
// ==================================================================================== //
const unsigned char PROGMEM thermometer_icon[] = { // Symmetrical 16x16 Thermometer Icon Bitmap
    0x07, 0x00,
    0x08, 0x80,
    0x08, 0x80,
    0x0a, 0x80,
    0x0a, 0x80,
    0x0a, 0x80,
    0x0a, 0x80,
    0x0a, 0x80,
    0x0a, 0x80,
    0x1f, 0xc0,
    0x3f, 0xe0,
    0x3f, 0xe0,
    0x3f, 0xe0,
    0x3f, 0xe0,
    0x1f, 0xc0,
    0x07, 0x00
};

const unsigned char PROGMEM droplet_icon[] = { // Symmetrical 16x16 Water Droplet Icon Bitmap
    0x01, 0x00,
    0x03, 0x80,
    0x07, 0xc0,
    0x0f, 0xe0,
    0x1f, 0xf0,
    0x1f, 0xf0,
    0x3f, 0xf8,
    0x3f, 0xf8,
    0x7f, 0xfc,
    0x7f, 0xfc,
    0x7f, 0xfc,
    0x7f, 0xfc,
    0x3f, 0xf8,
    0x3f, 0xf8,
    0x1f, 0xf0,
    0x07, 0xc0
};

// ==================================================================================== //
// SECTION 3: SYSTEM DATA STRUCTURES AND GLOBAL OBJECT INSTANCES                        //
// ==================================================================================== //
struct ClimateReading {
    float temperatureC;              // Temperature measurement expressed in Celsius
    float temperatureF;              // Temperature converted to Fahrenheit
    float temperatureK;              // Temperature converted to Kelvin
    float humidityRH;                // Relative humidity percentage measurement
    float heatIndexC;                // Calculated apparent heat index in Celsius
    float dewPointC;                 // Calculated atmospheric dew point temperature in Celsius
    float absHumidity;               // Calculated absolute humidity value in grams per cubic meter
    bool  isReadingValid;            // Validity flag indicating hardware data acquisition state
    bool  isComfortable;             // Evaluated Boolean flag for ambient human comfort
};

struct SystemStatus {
    uint32_t freeHeapBytes;          // Available system RAM capacity measured in bytes
    uint32_t bootCount;              // Cumulative reboot counter stored across wake cycles
    int      resetReason;            // Internal hardware code identifying trigger of system boot
};

DHT dht(DHTPIN, DHTTYPE);            // Initialize global DHT sensor hardware instance
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // OLED driver instance
ClimateReading currentClimate;       // Holds latest processed sensor readings and calculations
SystemStatus   sysStatus;            // Holds runtime system diagnostics and memory statistics

// ==================================================================================== //
// SECTION 4: CLIMATE COMPUTATIONS AND MATHEMATICAL FORMULAS                             //
// ==================================================================================== //
float convertCelsiusToFahrenheit(float c) {
    return (c * 1.8f) + 32.0f;       // Converts Celsius temperature to Fahrenheit scale
}

float convertCelsiusToKelvin(float c) {
    return c + 273.15f;              // Converts Celsius temperature to absolute Kelvin scale
}

float calculateDewPoint(float tempC, float humRH) {
    if (isnan(tempC) || isnan(humRH)) return 0.0f; // Guard against uninitialized sensor data
    float a = 17.27f;
    float b = 237.7f;
    float alpha = ((a * tempC) / (b + tempC)) + log(humRH / 100.0f); // Magnus formula component
    return (b * alpha) / (a - alpha); // Returns dew point temperature in Celsius
}

float calculateHeatIndex(float tempC, float humRH) {
    if (isnan(tempC) || isnan(humRH)) return 0.0f; // Guard against NaN inputs
    float hi = -8.78469478f + 1.61139411f * tempC + 2.33854883f * humRH;
    hi += -0.14611605f * tempC * humRH - 0.01230809f * (tempC * tempC);
    hi += -0.01642482f * (humRH * humRH) + 0.00221173f * (tempC * tempC) * humRH; // Rothfusz regression
    return hi;
}

float calculateAbsoluteHumidity(float tempC, float humRH) {
    if (isnan(tempC) || isnan(humRH)) return 0.0f; // Guard against invalid readings
    float vaporPressure = (6.112f * exp((17.67f * tempC) / (tempC + 243.5f))) * (humRH / 100.0f);
    return (vaporPressure * 216.7f) / (273.15f + tempC); // Returns vapor density in g/m³
}

bool evaluateComfortIndex(float tempC, float humRH) {
    bool tempOk = (tempC >= COMFORT_TEMP_MIN) && (tempC <= COMFORT_TEMP_MAX); // Check temperature limits
    bool humOk  = (humRH >= COMFORT_HUM_MIN)  && (humRH <= COMFORT_HUM_MAX);  // Check humidity limits
    return (tempOk && humOk);        // True if ambient temperature and humidity are inside comfort zone
}

// ==================================================================================== //
// SECTION 5: HARDWARE DIAGNOSTICS AND TELEMETRY LOGGING                                //
// ==================================================================================== //
void printFirmwareBanner(const char* firmwareVersion) {
    Serial.println(F("****************************************************"));
    Serial.println(F("*         ESP32-C3 CLIMATE MONITOR FIRMWARE        *"));
    Serial.print(F(  "*         VERSION: ")); Serial.print(firmwareVersion); Serial.println(F("                           *"));
    Serial.println(F("****************************************************"));
}

void updateSystemDiagnostics(SystemStatus &status) {
    status.freeHeapBytes = ESP.getFreeHeap();     // Query remaining available internal RAM
    status.resetReason   = (int)esp_reset_reason(); // Identify hardware reset vector reason
}

bool performI2CBusScan() {
    byte error;
    bool deviceFound = false;
    Serial.println("[I2C DIAGNOSTIC] Scanning bus for connected display module...");
    Wire.beginTransmission(OLED_I2C_ADDRESS);     // Ping specified hardware address
    error = Wire.endTransmission();
    if (error == 0) {
        Serial.print("[I2C DIAGNOSTIC] Active device detected at address 0x");
        Serial.println(OLED_I2C_ADDRESS, HEX);    // Log successful handshake
        deviceFound = true;
    } else {
        Serial.print("[I2C DIAGNOSTIC] Bus communication failure. Error code: ");
        Serial.println(error);                    // Log I2C hardware fault code
    }
    return deviceFound;
}

void readEnvironmentSensor(ClimateReading &data) {
    Serial.println("[SENSOR] Querying telemetry data from DHT22 module...");
    data.temperatureC = dht.readTemperature();   // Read ambient temperature in Celsius
    data.humidityRH   = dht.readHumidity();      // Read relative humidity percentage

    if (isnan(data.temperatureC) || isnan(data.humidityRH)) {
        Serial.println("[SENSOR ERROR] Reading failed! Invalid data (NaN) returned.");
        data.isReadingValid   = false;
        data.isComfortable    = false;
        data.temperatureF     = 0.0f;
        data.temperatureK     = 0.0f;
        data.heatIndexC       = 0.0f;
        data.dewPointC        = 0.0f;
        data.absHumidity      = 0.0f;
    } else {
        data.isReadingValid   = true;
        data.temperatureF     = convertCelsiusToFahrenheit(data.temperatureC);
        data.temperatureK     = convertCelsiusToKelvin(data.temperatureC);
        data.dewPointC        = calculateDewPoint(data.temperatureC, data.humidityRH);
        data.heatIndexC       = calculateHeatIndex(data.temperatureC, data.humidityRH);
        data.absHumidity      = calculateAbsoluteHumidity(data.temperatureC, data.humidityRH);
        data.isComfortable    = evaluateComfortIndex(data.temperatureC, data.humidityRH);
        Serial.println("[SENSOR SUCCESS] Telemetry parameters recorded successfully.");
    }
}

void logDiagnosticTelemetry(const ClimateReading &data, const SystemStatus &status) {
    Serial.println("====================================================");
    Serial.println("             SYSTEM TELEMETRY REPORT                ");
    Serial.println("====================================================");
    Serial.print(" Available Free Heap RAM: "); Serial.print(status.freeHeapBytes); Serial.println(" bytes");
    Serial.print(" ESP32 Reset Reason Code: "); Serial.println(status.resetReason);
    Serial.println("----------------------------------------------------");
    Serial.print(" Ambient Temperature (C): "); Serial.println(data.temperatureC, 2);
    Serial.print(" Ambient Temperature (F): "); Serial.println(data.temperatureF, 2);
    Serial.print(" Ambient Temperature (K): "); Serial.println(data.temperatureK, 2);
    Serial.print(" Relative Humidity (%):   "); Serial.println(data.humidityRH, 2);
    Serial.print(" Absolute Humidity:       "); Serial.print(data.absHumidity, 2); Serial.println(" g/m3");
    Serial.print(" Calculated Dew Point:    "); Serial.println(data.dewPointC, 2);
    Serial.print(" Calculated Heat Index:   "); Serial.println(data.heatIndexC, 2);
    Serial.print(" Environment Status:      "); Serial.println(data.isComfortable ? "COMFORTABLE" : "UNCOMFORTABLE");
    Serial.println("====================================================");
}

// ==================================================================================== //
// SECTION 6: DISPLAY GRAPHICS ENGINE AND USER INTERFACE ROUTINES                       //
// ==================================================================================== //
void drawVectorFace(int x, int y, bool isHappy) {
    int centerX = x + 16;            // Calculate vertical center of face boundary
    int centerY = y + 18;            // Lowered center offset for proper screen positioning

    display.drawCircle(centerX, centerY, 15, WHITE);          // Draw outer circular head profile
    display.fillCircle(centerX - 5, centerY - 5, 2, WHITE);   // Draw left eye pupil
    display.fillCircle(centerX + 5, centerY - 5, 2, WHITE);   // Draw right eye pupil

    if (isHappy) {
        // Draw smiling mouth vector curve
        display.drawLine(centerX - 7, centerY + 4, centerX - 4, centerY + 8, WHITE);
        display.drawLine(centerX - 4, centerY + 8, centerX + 4, centerY + 8, WHITE);
        display.drawLine(centerX + 4, centerY + 8, centerX + 7, centerY + 4, WHITE);
    } else {
        // Draw frowning mouth vector curve
        display.drawLine(centerX - 7, centerY + 8, centerX - 4, centerY + 4, WHITE);
        display.drawLine(centerX - 4, centerY + 4, centerX + 4, centerY + 4, WHITE);
        display.drawLine(centerX + 4, centerY + 4, centerX + 7, centerY + 8, WHITE);
    }
}

void renderTemperatureBlock(float temp) {
    display.drawBitmap(0, 4, thermometer_icon, 16, 16, WHITE); // Render thermometer icon lowered to y=4

    display.setTextSize(1);          // Standard text size for label descriptor
    display.setCursor(22, 2);        // Align label horizontally adjacent to icon
    display.print("Temperature");    // Display title-cased label "Temperature"

    display.setTextSize(2);          // Large bold font size for numerical telemetry data
    display.setCursor(22, 12);       // Position reading value directly below header label
    display.print(temp, 1);          // Print temperature rounded to one decimal place
    display.write(248);              // Send Code Page 437 degree symbol character code (°)
    display.print("C");              // Append Celsius unit indicator
}

void renderHumidityBlock(float hum) {
    display.drawBitmap(0, 36, droplet_icon, 16, 16, WHITE); // Render water droplet icon lowered to y=36

    display.setTextSize(1);          // Standard text size for label descriptor
    display.setCursor(22, 34);       // Align label horizontally adjacent to droplet icon
    display.print("Humidity");       // Display title-cased label "Humidity"

    display.setTextSize(2);          // Large bold font size for numerical telemetry data
    display.setCursor(22, 44);       // Position reading value directly below humidity label
    display.print(hum, 1);           // Print relative humidity rounded to one decimal place
    display.print("%");              // Append percentage unit symbol
}

void renderErrorScreen() {
    display.clearDisplay();          // Wipe graphics buffer memory
    display.setTextSize(2);          // Set large text scale for error notification
    display.setTextColor(WHITE);
    display.setCursor(10, 24);       // Center error message on screen
    display.println("ERROR");        // Render failure alert message
    display.display();               // Flush graphics frame buffer to display
}

void renderMainGUI(const ClimateReading &data) {
    display.clearDisplay();          // Wipe graphics buffer memory prior to redraw
    display.setTextColor(WHITE);     // Set monochromatic active pixel color
    display.cp437(true);             // Enable extended IBM CP437 character mapping

    if (!data.isReadingValid) {
        renderErrorScreen();        // Fallback to error alert layout if sensor fails
        return;
    }

    renderTemperatureBlock(data.temperatureC); // Draw temperature icon, label, and values
    renderHumidityBlock(data.humidityRH);      // Draw humidity icon, label, and values
    
    // Draw status expression face positioned on the right side (y=28 offset aligns bottom)
    drawVectorFace(94, 28, data.isComfortable);

    display.display();               // Push frame buffer contents to physical OLED panel
}

// ==================================================================================== //
// SECTION 7: POWER MANAGEMENT AND DEEP SLEEP SUBSYSTEM                                 //
// ==================================================================================== //
void configurePowerDomain() {
    Serial.println("[POWER] Shutting down non-essential internal buses for power saving...");
}

void executeDeepSleepRoutine(uint32_t seconds) {
    uint64_t sleepMicros = (uint64_t)seconds * uS_TO_S_FACTOR; // Compute microsecond duration
    esp_sleep_enable_timer_wakeup(sleepMicros);               // Configure RTC wakeup timer

    Serial.print("Entering deep sleep for ");
    Serial.print(seconds);
    Serial.println(" seconds...");
    Serial.flush();                  // Ensure serial transmit channel empties before power-down

    esp_deep_sleep_start();          // Enter ultra-low-power deep sleep mode
}

// ==================================================================================== //
// SECTION 8: FIRMWARE ENTRY POINT ROUTINES (SETUP & LOOP)                              //
// ==================================================================================== //
void setup() {
    Serial.begin(115200);            // Initialize high-speed UART debugging output
    delay(100);                      // Short settling delay for power bus stabilization

    printFirmwareBanner("3.6.0-PROD"); // Output system identification banner to terminal

    updateSystemDiagnostics(sysStatus);  // Collect boot reason and memory statistics
    Wire.begin(SDA_PIN, SCL_PIN);        // Initialize I2C hardware controller bus
    performI2CBusScan();                 // Validate hardware presence on I2C bus

    dht.begin();                         // Initialize DHT22 environmental sensor hardware
    delay(1000);                         // Sensor power-on warm-up delay

    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
        Serial.println("[OLED SUCCESS] SSD1306 panel connected and initialized.");

        readEnvironmentSensor(currentClimate);             // Acquire raw climate telemetry
        logDiagnosticTelemetry(currentClimate, sysStatus);  // Log metrics to Serial
        renderMainGUI(currentClimate);                     // Render complete GUI layout
    } else {
        Serial.println("[OLED ERROR] Display controller initialization failed!");
    }

    configurePowerDomain();                                // Power down active buses
    executeDeepSleepRoutine(SLEEP_INTERVAL_SEC);           // Put processor to deep sleep
}

void loop() {
    // Left empty: Execution cycle ends in setup() via deep sleep wake cycle
}