#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// =========================================================================
// 1. CONFIGURATION
// =========================================================================
const char* ssid = "Your Wifi SSID";
const char* password = "Your Wifi password";

// Replace this with the Token given by @BotFather on Telegram
const String BOT_TOKEN = "Your bot token";

// Add your Chat ID here (keep the quotes)
const String AUTHORIZED_CHAT_ID = "Your chat ID";

// =========================================================================
// 2. HARDWARE
// =========================================================================
const int irPin     = 27;
const int buzzerPin = 26;

// =========================================================================
// 3. SHARED STATE BETWEEN BOTH CORES (volatile + Mutex)
// =========================================================================
volatile bool systemArmed      = true;
volatile bool alarmActive      = false;
volatile bool armingPending    = false;
volatile unsigned long scheduledArmingTime = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; // Lock for concurrent access

// =========================================================================
// 4. COMMUNICATION
// =========================================================================
WiFiClientSecure secureClient;
UniversalTelegramBot bot(BOT_TOKEN, secureClient);

// =========================================================================
// 5. TELEGRAM MESSAGE HANDLER
// =========================================================================
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;

    if (chat_id != AUTHORIZED_CHAT_ID) {
      bot.sendMessage(chat_id, "⛔ Access denied.", "");
      continue;
    }

    if (text == "Status") {
      String status = "System Status:\n";
      status += systemArmed  ? "🛡️ SYSTEM ARMED\n"    : "💤 SYSTEM IN SLEEP MODE\n";
      status += alarmActive ? "🚨 ALARM ONGOING!\n" : "✅ NO FLAME.\n";
      bot.sendMessage(chat_id, status, "");
    }

    else if (text == "Turn off alarm") {
      portENTER_CRITICAL(&mux);
        alarmActive = false;
        systemArmed  = false;
      portEXIT_CRITICAL(&mux);
      digitalWrite(buzzerPin, LOW);
      bot.sendMessage(chat_id, "🔥 Alarm deactivated. System in sleep mode.", "");
    }

    else if (text == "Activate alarm") {
      // Send message BEFORE arming, without using blocking delay()
      bot.sendMessage(chat_id, "🛡️ Activation in 15s.......", "");
      
      // Non-blocking timer: record future arming time
      // The detection core will check this value
      scheduledArmingTime = millis() + 15000;
      armingPending = true;
    }
  }
}

// =========================================================================
// 6. CORE 0 TASK — IR Detection + Buzzer (Real-time)
// =========================================================================
void detectionTask(void* param) {
  unsigned long lastBeep      = 0;
  bool          buzzerState   = false;
  bool          lastIRState   = HIGH;  // For debouncing
  unsigned long debounceTime  = 0;
  const int     DEBOUNCE_MS   = 50;    // Ignore transitions < 50ms

  while (true) {
    bool irReading = digitalRead(irPin);
    unsigned long now = millis();

    // --- IR Sensor Debouncing ---
    if (irReading != lastIRState) {
      debounceTime = now; // Reset timer on every change
    }
    if ((now - debounceTime) > DEBOUNCE_MS) {
      // Signal stable for DEBOUNCE_MS -> process it
      if (irReading == LOW) {  // Obstacle detected
        portENTER_CRITICAL(&mux);
        bool armed = systemArmed;
        bool alreadyAlarming = alarmActive;
        portEXIT_CRITICAL(&mux);

        if (armed && !alreadyAlarming) {
          portENTER_CRITICAL(&mux);
            alarmActive = true;
          portEXIT_CRITICAL(&mux);
          Serial.println(">>> FLAME DETECTED <<<");
          // Note: calling sendMessage here would be dangerous (WiFi runs on Core 1)
          // We just raise the flag, Telegram will detect it on Core 1
        }
      }
    }
    lastIRState = irReading;

    // --- Delayed Arming (Replaces delay(15000)) ---
    if (armingPending && now >= scheduledArmingTime) {
      portENTER_CRITICAL(&mux);
        systemArmed        = true;
        alarmActive        = false;
        armingPending      = false;
      portEXIT_CRITICAL(&mux);
      Serial.println("System ARMED.");
    }

    // --- Non-blocking Buzzer ---
    portENTER_CRITICAL(&mux);
    bool currentAlarmStatus = alarmActive;
    portEXIT_CRITICAL(&mux);

    if (currentAlarmStatus) {
      if (now - lastBeep > 300) {
        buzzerState = !buzzerState;
        digitalWrite(buzzerPin, buzzerState ? HIGH : LOW);
        lastBeep = now;
      }
    } else {
      // Ensure buzzer is OFF if alarm stopped
      if (buzzerState) {
        buzzerState = false;
        digitalWrite(buzzerPin, LOW);
      }
    }

    vTaskDelay(1); // Yield 1ms to FreeRTOS scheduler (prevents watchdog trigger)
  }
}

// =========================================================================
// 7. CORE 1 TASK — Telegram + Alarm Notification
// =========================================================================
void telegramTask(void* param) {
  unsigned long lastCheck       = 0;
  bool          alertSent       = false; // Prevents message spamming
  bool          armConfirmSent  = false;

  while (true) {
    unsigned long now = millis();

    // --- Alarm Notification (Single send) ---
    if (alarmActive && !alertSent) {
      bot.sendMessage(AUTHORIZED_CHAT_ID, "🚨 Alert: Flame detected by the Flame sensor! 🚨", "");
      alertSent = true;
    }
    if (!alarmActive) alertSent = false; // Reset if alarm stopped

    // --- Arming Confirmation Notification ---
    if (!armingPending && armConfirmSent == false && systemArmed) {
      // Logic for one-time confirmation after arming
    }

    // --- Telegram Polling (Every 2s, less aggressive) ---
    if (now - lastCheck > 2000) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      while (numNewMessages) {
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      lastCheck = now;
    }

    vTaskDelay(10);
  }
}

// =========================================================================
// 8. SETUP
// =========================================================================
void setup() {
  Serial.begin(115200);
  pinMode(irPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected!");

  secureClient.setInsecure();
  bot.sendMessage(AUTHORIZED_CHAT_ID, "🔌 System Online & Armed.", "");

  // Launching the two tasks on their respective cores
  xTaskCreatePinnedToCore(detectionTask, "Detection", 4096, NULL, 2, NULL, 0); // Core 0, Priority 2
  xTaskCreatePinnedToCore(telegramTask,  "Telegram",  8192, NULL, 1, NULL, 1); // Core 1, Priority 1
}

// =========================================================================
// 9. LOOP (Empty — FreeRTOS tasks handle everything)
// =========================================================================
void loop() {
  vTaskDelay(1000);
}