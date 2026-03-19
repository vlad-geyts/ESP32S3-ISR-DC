#include <Arduino.h>

// Handle for the Binary Semaphore
SemaphoreHandle_t panicSemaphore;

// GPIO Definitions
const int BUTTON_PIN = 47; // Moved from 19 to 47
const int LED_PIN = 2;

// Function Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);

void setup() {
    delay(1000);
    Serial.begin(115200);
    delay(500);

    Serial.println("\n--- Phase 3: ISR & Semaphores (GPIO 47) ---");

    // 1. Create the Binary Semaphore
    panicSemaphore = xSemaphoreCreateBinary();

    // 2. Configure GPIO 47 with Internal Pull-up
    pinMode(BUTTON_PIN, INPUT_PULLUP);

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
    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Notify the task that the button was pressed
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    // If the panic task has higher priority, this forces a context switch 
    // immediately upon exiting the ISR for "instant" feel.
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// --- Tasks ---

void panicTask(void *pvParameters) {
    for(;;) {
        // Task stays in "Blocked" state (0% CPU) until semaphore is given
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            Serial.println("\n[ISR] !!! PANIC TRIGGERED ON GPIO 47 !!!");
            
            // Visual feedback: Fast strobe
            for(int i = 0; i < 20; i++) {
                digitalWrite(LED_PIN, !digitalRead(LED_PIN));
                vTaskDelay(pdMS_TO_TICKS(40)); 
            }
            
            Serial.println("[Panic] System stabilized. Returning to Heartbeat.");
            
            // Small debounce delay to prevent multiple triggers from one physical press
            vTaskDelay(pdMS_TO_TICKS(500));
            // Clear any accidental "bounces" that happened during the strobe
            xSemaphoreTake(panicSemaphore, 0); 
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