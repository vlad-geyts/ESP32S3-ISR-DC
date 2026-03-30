#include <Arduino.h>
#include <Preferences.h> // Include the NVS wrapper

Preferences prefs;

// Handle for the Binary Semaphore
SemaphoreHandle_t panicSemaphore;

namespace Config {
    // 'constexpr' tells the compiler this value is known at compile-time.
    // It is more efficient than 'const' and safer than '#define'.
    constexpr int LedPin    = 2;
    constexpr int ButtonPin = 47;
    constexpr int StrobPin    = 2;
    constexpr int PanicPin    = 2;
}

// Function Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);

void setup() {
    delay(1000);
    Serial.begin(115200);
    delay(5000);

    Serial.println("\n--- Connected via CH343 UART (COM?) ---");
    Serial.println(  "--- ESP32-S3 Dual Core Booting --------");
    Serial.println("\n--- ESP Hardware Info------------------");
    
    // Display ESP Information
    Serial.printf("Chip ID: %u\n", ESP.getChipModel()); // Get the 24-bit chip ID
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz()); // Get CPU frequency
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion()); // Get SDK version

    // Get and print flash memory information
    Serial.printf("Flash Chip Size: %u bytes\n", ESP.getFlashChipSize()); // Get total flash size
   
    // Get and print SRAM memory information
    Serial.printf("Internal free Heap at setup: %d bytes\n", ESP.getFreeHeap());
    if(psramFound()){
        Serial.printf("Total PSRAM Size: %d bytes", ESP.getPsramSize());
    } else {
         Serial.print("No PSRAM found");
    }
    Serial.println("\n---------------------------------------");
    Serial.println("\n");
    
    // Configure Hardware using our Namespace
    pinMode(Config::LedPin, OUTPUT);
    pinMode(Config::ButtonPin, INPUT_PULLUP);
    pinMode(Config::StrobPin, OUTPUT);
    digitalWrite(Config::StrobPin, LOW);



    // Reading "Panic Count" on start up
    prefs.begin("system", true); // Open in Read-Only mode
    Serial.printf("Total Lifetime Panic Events: %u\n", prefs.getUInt("panic_count", 0));
    prefs.end();

    Serial.println("\n--- Phase 3: ISR & Semaphores ---");

    // 1. Create the Binary Semaphore
    panicSemaphore = xSemaphoreCreateBinary();

    // 2. Attach Interrupt to (ButtonPin)
    attachInterrupt(digitalPinToInterrupt(Config::ButtonPin), handleButtonInterrupt, FALLING);

    // 4. Panic Handler Task (Highest Priority: 3) on Core 1
    xTaskCreatePinnedToCore(panicTask, "Panic", 4096, NULL, 3, NULL, 1);

    // 5. Standard Heartbeat (Priority: 1) on Core 0
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 1, NULL, 0);
}

void loop() {
    // Arduino task is no longer needed
    vTaskDelete(NULL);
}

// --- Interrupt Service Routine (ISR) ---
// IRAM_ATTR ensures this runs from RAM, critical for S3 stability
void IRAM_ATTR handleButtonInterrupt() {

    // Set GPIO 0 high to indicate that we are jumped into ISR
    digitalWrite(Config::StrobPin, HIGH);

    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Notify the task that the button was pressed
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    // If the panic task has higher priority, this forces a context switch 
    // immediately upon exiting the ISR for "instant" feel.

    // Clear GPIO 0 on exit from ISR
    digitalWrite(Config::StrobPin, LOW);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }

}

// --- Tasks ---

void panicTask(void *pvParameters) {
    for(;;) {
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            detachInterrupt(digitalPinToInterrupt(Config::ButtonPin));
            digitalWrite(45, HIGH); // Your PANIC monitor signal

            // --- NVS Logic ---
            prefs.begin("system", false); // Open "system" namespace in R/W mode
            uint32_t count = prefs.getUInt("panic_count", 0); // Get existing or default to 0
            count++;
            prefs.putUInt("panic_count", count); // Save new count to Flash
            prefs.end();

            Serial.printf("\n[Panic] Event #%u recorded in NVS!\n", count);

            // Your strobe feedback logic...
            for(int i = 0; i < 20; i++) {
                digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            digitalWrite(45, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            attachInterrupt(digitalPinToInterrupt(Config::ButtonPin), handleButtonInterrupt, FALLING);
        }
    }
}

void heartbeatTask(void *pvParameters) {
     for(;;) {
        digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
        Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis()/1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}