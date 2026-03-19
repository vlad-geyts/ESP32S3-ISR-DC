The Concept: Deferred Interrupt Processing

We want the ISR to be as fast as possible. Instead of printing to Serial or doing logic inside the ISR (which can crash the chip), the ISR will simply "give" a semaphore and exit. A dedicated task will be "blocked" (sleeping) waiting for that semaphore.

Why this is a "Senior" implementation:

    1) IRAM_ATTR: This tells the compiler to place the ISR code in the S3's fast internal RAM instead of Flash. If the CPU had to fetch the ISR from Flash while the Flash bus was busy, the system would crash with a "Cache Disabled" error.

    2) xSemaphoreGiveFromISR: FreeRTOS has specific functions for use inside interrupts. The FromISR version is designed to be non-blocking and safe for the kernel's state.

    3) Task Priority: Notice panicTask has a priority of 3 (higher than Heartbeat). The moment the button is pressed, the scheduler pauses everything else to handle the panic.

    4) Hardware Debouncing: In a real product, you'd add a small RC filter or a software debounce timer, as mechanical buttons "bounce" and might trigger the ISR multiple times.

    What to look for in this test:

    1) Latency: Because of portYIELD_FROM_ISR(), the very next instruction the CPU executes after the interrupt finishes is the first line of the panicTask. This is the hallmark of real-time systems.

    2) Debouncing: Mechanical switches are "noisy." You might see the panic trigger twice if you tap the wire quickly. I added a xSemaphoreTake(panicSemaphore, 0) at the end of the panic routine—this is a common trick to "flush" any extra interrupt flags caused by contact bounce during the execution.