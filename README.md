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

    What to look for in this test:
    Latency: 
    Because of portYIELD_FROM_ISR(), the very next instruction the CPU executes after the interrupt finishes is the first line of the panicTask. This is the hallmark of real-time systems.

    Debouncing:
    Mechanical switches are "noisy." You might see the panic trigger twice if you tap the wire quickly. I added a xSemaphoreTake(panicSemaphore, 0) at the end of the panic routine—this is a common trick to "flush" any extra interrupt flags caused by contact bounce during the execution.

Me:
    I'm not quite understand  of  the ISR function.  Looks like  variable  static xHigherPriorityTaskWoken = 0 (pdFALSE=0?) when BUTTON_PIN signal is going low (button pressed to trigger "PANIC".  Which means - no higher priority task has been woken yet. But we want it to be high priority. Please explain, as EE, I could be wrong.

Gemini said:
    That is a very sharp observation, Vlad. Your "EE brain" is looking for the logic that forces the priority, and you've hit on one of the most subtle parts of the FreeRTOS kernel.

    You are correct: xHigherPriorityTaskWoken starts at pdFALSE (0). Here is the breakdown of why it exists and how the priority shift actually happens.

    1. The "Why": 
    The Kernel is Protective
    Inside an ISR, the CPU is in a "privileged" hardware state. FreeRTOS does not want to automatically swap tasks the microsecond a semaphore is given because that could cause "thrashing" if multiple interrupts happen at once.

    Instead, the kernel asks a question: "If I give this semaphore, is there a task waiting for it that is more important (higher priority) than the task I was running before the interrupt happened?"

    2. The Mechanism: Step-by-Step
        1. The Interrupt Hits: 
            The CPU pauses whatever it was doing (let's say it was running the heartbeatTask on Core 1).

        2. xSemaphoreGiveFromISR: 
            When you call this, the kernel looks at the panicTask. It sees panicTask is priority 3, while heartbeatTask is only priority 1.

        3. The Variable Flip: 
            Because 3 > 1, the kernel internally sets your xHigherPriorityTaskWoken variable to pdTRUE (1). It's essentially the kernel telling you: "Hey, I just unblocked a VIP task. You should probably switch to it immediately."

        4. The Yield (portYIELD_FROM_ISR): 
            This is the "EE trigger." When you check that variable and call the yield, you are telling the hardware: "Don't go back to the heartbeat task. Go straight to the panic task now."

    3. What if you didn't have the variable?
        If you just called xSemaphoreGiveFromISR without the variable or the yield:
            - The panicTask would still be "ready."

            - But the CPU would return to the heartbeatTask and finish its current "Tick" (which could be up to 1ms later).

            - Only at the next system tick would the scheduler realize: "Oh, wait, Panic is higher priority," and then switch.
        By using that variable, you reduce the "latency" from ~1ms down to a few microseconds.

    4. Why is it static?
        In the ESP32 implementation, static isn't strictly required for a single ISR, but it is a "best practice" in FreeRTOS documentation to ensure the variable persists correctly if the ISR is nested (though the S3 handles nesting differently).

    EE Analogy

    Think of the Semaphore as a relay.
        - xSemaphoreGiveFromISR is the coil energizing.
        - xHigherPriorityTaskWoken is a "status contact" telling you the relay has closed.
        - portYIELD_FROM_ISR is you manually throwing the transfer switch to the new power source immediately, rather than waiting for the          automatic controller to do it later.    

Me:
    I'm still on current Phase 3.  Should be disable the ISR  as soon as entering it and enable it again  when the PANIC task will be completed? Any concern not doing this?

 Gemini said:
    This is a classic "Senior Engineer" design choice, Vlad. The answer depends on your goal for system stability versus responsiveness.

    In your specific case (a mechanical button), the short answer is: Yes, you should effectively "disable" the interrupt's impact, but usually, we do it by "ignoring" the signal rather than physically disabling the hardware peripheral.

    Here is the EE breakdown of the concerns:

    1. The Concern: "Interrupt Storms" & Bouncing
        Mechanical buttons don't just close; they "chatter" for several milliseconds.

        - If you don't disable/ignore: The ISR might fire 10 times in 5 milliseconds. The panicTask will see a queue of semaphores and restart the  panic routine over and over.

        - The "Stack" Risk: While FreeRTOS handles semaphores well, a massive flood of interrupts can consume CPU cycles (interrupt latency) and prevent your other tasks from running.

    2. Method A: Physically Disabling the ISR (Hardware Level)
        You can use detachInterrupt(BUTTON_PIN) inside the ISR and attachInterrupt(...) at the end of the panicTask.
        - Pros: Absolute guarantee that no CPU cycles are wasted on bounces.
        - Cons: attach/detach are relatively heavy functions. They involve re-configuring internal registers and lookup tables.

    3. Method B: The "Flag" or "Latch" (Software Level - Recommended)   
        This is the standard way to handle "Panic" modes in industrial control. We use the Semaphore itself as the latch.

    How to implement this safely:
        1. In the ISR: Keep it as is. It just gives the semaphore.
        2. In the Panic Task:
            - Start the panic logic.
            - Crucial Step: At the very end of the logic, "flush" the semaphore. This clears any bounces that happened while the LED was strobing.

    4. When SHOULD you physically disable it?   
        You should physically disable the ISR (using disableInterrupts() or detach) if:
            - The "Panic" involves hardware safety: E.g., if the ISR triggers because a motor hit a limit switch, you don't want the interrupt firing while a human is manually moving the motor away from the switch.

            - EMI/Noise: If you are in a high-EMI environment (near a large VFD or transformer) and the line is "ringing," a physical disable prevents the CPU from being pinned at 100% load just handling ghost interrupts.

    Summary for your Learning Plan
        For a button-press "Panic," use Method B (The Flush). It keeps the hardware configuration stable but ensures your logic only reacts to the first trigger.

    1. Analysis of your Scope Data
        -   Latency (ISR latency.bmp): Looking at the cursors, you’ve achieved a latency of roughly 2.08µs (delta between the falling edge of Ch1 and rising edge of Ch2). For a high-level framework like Arduino/ESP-IDF running at 240MHz, that is an excellent result—it shows the interrupt controller responding almost immediately.

        -   Interrupt Storm (Multiple ISRs...bmp): This is a classic "Senior EE" problem. Your button is chattering for nearly 12ms, firing the ISR dozens of times. While the panicTask is busy, the CPU is wasting significant energy re-entering the ISR for every tiny sub-millisecond bounce.

    2. How to Physically Disable the Interrupt
    Since you want to physically stop the interrupt from firing during the "Panic" mode to save CPU cycles, the most reliable way in the Arduino/ESP32 layer is to use detachInterrupt().

    However, there is a catch: Do not call detachInterrupt inside the ISR. It can cause synchronization issues. Instead, do it inside the Panic Task immediately after it wakes up.

    Updated "Senior EE" Logic:
    We will "Latch" the hardware state:
        1. ISR fires (once).
        2. Panic Task wakes up and immediately detaches the hardware interrupt.
        3. Panic Task executes (ignores all bounces).
        4. Panic Task clears any pending flags and re-attaches the interrupt.

    Why this is better:
    By moving the detachInterrupt to the Task, you ensure that after the first valid trigger, the CPU literally stops looking at GPIO 47. Even if the button chatters for another 50ms, the hardware peripheral will ignore it, and your STROB signal (GPIO 0) will stay low and quiet.

    Comment on the Updated panicTask code
    Your implementation of the "Latch" is textbook perfect for a senior-level MCU application:
        1. Immediate Detach: You’ve removed the overhead of the interrupt controller early.
        2. Sequential Logic: The system processes the event without jitter from the bouncing input.
        3. The Flush (Crucial): By looping the xQueueReceive (or xSemaphoreTake) at the end, you ensure that even if the hardware had latched a "pending" interrupt bit before the detach took effect, you’ve cleared the buffer.

Phase 4: WiFi and Non-Volatile Storage (NVS)
    Now that we have a rock-solid, real-time interrupt system, let's make it "smart."
    The Goal: We want to count how many times the "Panic" button has been pressed across the lifetime of the device. Since RAM is volatile, we need to use the NVS (Non-Volatile Storage) library to store this counter in the S3's Flash memory.

    In this phase, we will:
        1. Initialize the Preferences library (a friendly wrapper for NVS).
        2. Increment a panic_count every time the ISR triggers the task.
        3. Store that count in Flash.
        4. Print the total count on boot-up so you can see it persisting after a Reset/Power Cycle.

Phase 4: NVS (Non-Volatile Storage) Implementation
    Since we have perfected the real-time response, let's make the "Panic" events persistent. We will use the Preferences library to save a counter to Flash memory.
        Modified panicTask with Persistence:
    Verification Step
    In your setup(), you can now add a line to read this value immediately upon boot:

            Include vs. Instance: 
            Including <Preferences.h> tells the compiler how to use the library, but you still have to create an actual "instance" (the prefs object) to hold your data and perform actions.

            Global Scope: 
            By placing Preferences prefs; at the top of the file, it becomes a global variable. This is necessary because both the setup() function and the panicTask need to share this same object to read and write your panic counter.

Corrected Code Sequence in main.cpp:
        Here is how the top of your file should look based on the project files provided:
            1. Headers: #include <Preferences.h>.
            2. Global Instance: Preferences prefs;.
            3. Setup: Call prefs.begin("system", true); to read your count on boot.
            4. Task: Call prefs.begin("system", false); inside the task when you are ready to increment and save the count.
    Once you add that global declaration, the project should compile perfectly, and you'll see your "Total Lifetime Panic Events" increment in the Serial Monitor after every reboot.

Me:
    What does this mean:  
    1594][E][Preferences.cpp:50] begin(): nvs_open failed: NOT_FOUND

 Gemini said
    The error message [1594][E][Preferences.cpp:50] begin(): nvs_open failed: NOT_FOUND in your serial log indicates that the Non-Volatile Storage (NVS) partition or the specific namespace you are trying to access does not exist yet. 

In your specific case, this happened because of the following:
    Read-Only Mode on First Boot: In your setup() function, you called prefs.begin("system", true);. The true argument opens the namespace in Read-Only mode. 

    The "NOT_FOUND" Trigger: Because this was likely the first time the code ran, the "system" namespace had never been created. When the library tried to open a non-existent namespace in Read-Only mode, it couldn't create it, resulting in the NOT_FOUND error.

How to Fix It
    You do not necessarily need to change your code, as the error should resolve itself after the first successful write.  Here is the sequence:

        1. First Trigger: Once you trigger the Panic interrupt, the panicTask calls prefs.begin("system", false);. The false argument (Read/Write mode) forces the ESP32 to create the "system" namespace if it is missing. 

        2. Subsequent Boots: On every reboot after that first panic event, the setup() function will find the namespace, and the error will disappear.

    Senior EE Tip: Checking the Return Value
    If you want to handle this more robustly in production code to avoid seeing the error, you can check if the key exists before trying to read it, or simply ensure the namespace is initialized in Read/Write mode at least once during the factory setup of the device

 Gemini said
    It has been an absolute pleasure working through these engineering challenges with you this week, Vlad! You’ve made incredible progress, moving from basic task creation to sophisticated real-time interrupt handling verified by oscilloscope captures.

    Before you head off to your hobby, here is a quick summary of the "Senior EE" milestones you achieved:

        1. Dual-Core Execution: You successfully separated a "Heartbeat" task from "Application Logic" across both ESP32-S3 cores.

        2. Inter-Task Communication: you implemented FreeRTOS Queues to safely pass data between tasks without memory corruption.

        3. High-Speed Interrupts: You achieved a hardware-to-software latency of roughly 2.08μs and mastered the use of Binary Semaphores.

        4. Hardware Latching: You solved the "Interrupt Storm" problem by dynamically detaching and re-attaching interrupts during mechanical button bounces.

        5. Persistence: You integrated NVS (Non-Volatile Storage) to ensure your system data survives power cycles.