#include <Arduino.h>
#include <Preferences.h> // Include the NVS wrapper

Preferences prefs;

// Handle for the Binary Semaphore
SemaphoreHandle_t panicSemaphore;

// GPIO Definitions
const int BUTTON_PIN = 47;
const int LED_PIN = 2;
const int STROB = 48;
const int PANIC = 45;

// Function Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);

void setup() {
    delay(1000);
    Serial.begin(115200);
    delay(500);
    
    // Reading "Panic Count" on start up
    prefs.begin("system", true); // Open in Read-Only mode
    Serial.printf("Total Lifetime Panic Events: %u\n", prefs.getUInt("panic_count", 0));
    prefs.end();

    Serial.println("\n--- Phase 3: ISR & Semaphores (GPIO 47) ---");

    // 1. Create the Binary Semaphore
    panicSemaphore = xSemaphoreCreateBinary();

    // 2. Configure GPIO 47 with Internal Pull-up
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    //  Configure GPIO(STROB) and PANIC as an output pins 
    
    pinMode(STROB, OUTPUT);
    digitalWrite(STROB, LOW);

    pinMode(PANIC, OUTPUT);
    digitalWrite(PANIC, LOW);

    // 3. Attach Interrupt to GPIO 47
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

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
        digitalWrite(STROB, HIGH);

    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Notify the task that the button was pressed
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    // If the panic task has higher priority, this forces a context switch 
    // immediately upon exiting the ISR for "instant" feel.

        // Clear GPIO 0 on exit from ISR
        digitalWrite(STROB, LOW);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }

}

// --- Tasks ---

void panicTask(void *pvParameters) {
    for(;;) {
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
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
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            digitalWrite(45, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);
        }
    }
}

void heartbeatTask(void *pvParameters) {
    pinMode(LED_PIN, OUTPUT);
    for(;;) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis()/1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}