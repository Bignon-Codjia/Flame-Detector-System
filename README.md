# 🛡️ ESP32-FireGuard: Dual-Core Flame Alarm

**ESP32-FireGuard** is a high-reliability IoT security solution engineered to detect flames or physical intrusions using IR sensors. By leveraging the **ESP32’s dual-core architecture (FreeRTOS)**, this system ensures that critical safety monitoring is never delayed by network latency or WiFi processing.

---

##  Key Features

*  Dual-Core Execution Engine:
* **Core 0**: High-priority real-time polling (Detection & Buzzer).
* **Core 1**: Communication layer (WiFi & Telegram Bot API).


*  Thread-Safe State Management: Utilizes `portMUX_TYPE` mutexes to prevent race conditions during cross-core data sharing.
*  Remote Control: Fully manageable via Telegram commands (Arm/Disarm/Status).
*  Non-Blocking Logic: Zero `delay()` calls; the system remains responsive 100% of the time using asynchronous timers.
*  Smart Debouncing: Advanced signal filtering to eliminate false positives from IR noise.

---

##  Code Architecture

The firmware is designed with a **Task-Based Architecture** to ensure separation of concerns.

| Task | Core | Priority | Description |
| --- | --- | --- | --- |
| `detectionTask` | **0** | **2** | Handles IR sensor interrupts, debouncing, and the 15s arming countdown. |
| `telegramTask` | **1** | **1** | Manages WiFi connection, polls for Telegram updates, and sends alerts. |

### Logic Flow

1. **Detection**: Core 0 detects a signal. If the system is **Armed**, it sets a global `alarmActive` flag.
2. **Notification**: Core 1 observes the flag change and pushes an encrypted notification via the Telegram Bot API.
3. **Action**: The user can send a command to Core 1 to reset the flag on Core 0.

---
##  Installation

### 1. Prerequisites

Ensure you have the following libraries installed in your Arduino IDE or PlatformIO:

* `UniversalTelegramBot` (by Brian Lough)
* `ArduinoJson` (by Benoit Blanchon)

### 2. Configuration

Open the source code and update your credentials:

```cpp
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const String BOT_TOKEN = "YOUR_BOT_TOKEN_FROM_BOTFATHER";
const String AUTHORIZED_CHAT_ID = "YOUR_NUMERICAL_ID";

```

### 3. Deployment

1. Connect your ESP32.
2. Select the **DOIT ESP32 DEVKIT V1** board.
3. Upload and open the Serial Monitor at **115200 baud**.

---

##  User Guide

Once the system is online, use these commands in your Telegram chat:

* `Status`: Check if the system is **Armed** and if a flame is currently detected.
* `Activate alarm`: Arms the system. A **15-second delay** is provided to allow you to leave the area before monitoring begins.
* `Turn off alarm`: Instantly silences the buzzer and puts the system into **Sleep Mode**.
* NB: AT the start, the system is already activated

---
