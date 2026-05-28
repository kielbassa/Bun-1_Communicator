// Bluetooth libraries
#include <BluetoothSerial.h>
#include <string.h>

// NVS - non volatile storage - AES key persists on reboot
#include <Preferences.h>

// OLED libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <graphics.h>

// AES libraries
#include "encryption_aes.h"

// LoRa libraries
#include <LoRa.h>
#include <SPI.h>

// OLED pins
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

// LoRa pins
#define LORA_SCK  5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DI0  26

// Button
#define BUTTON_PIN 0

// LoRa frequency
#define LORA_FREQUENCY 866E6

// Debounce time in ms
#define DEBOUNCE_MS 200

// Max gap between two presses to count as a double press (ms)
#define DOUBLE_PRESS_MS 400

//state machine states
enum AppState {
  WAITING_BT,   // splash — held until a BT client connects
  MENU,
  OUTBOX_IDLE,
  OUTBOX_READY,
  SENDING,
  INBOX,
  CONFIG,
  CONFIG_EDIT_KEY   // waiting for new AES key over BT
};

AppState currentState = WAITING_BT;

//auxiliary variables
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
BluetoothSerial SerialBT;
Preferences prefs;

// Global variables
String deviceName = "";  // assigned at boot with a random suffix

String btBuffer      = "";   // buffer for incoming BT data
String messageToSend = "";   // message waiting in outbox to be sent via LoRa

// Inbox ring buffer — index 0 is always the newest message
#define INBOX_SIZE 5
String inboxMsgs[INBOX_SIZE];
int    inboxRssi[INBOX_SIZE];
int    inboxCount   = 0;  // number of stored messages (0..INBOX_SIZE)
int    inboxViewIdx = 0;  // index of the message currently on screen

bool lastButtonState        = HIGH;
unsigned long lastDebounceTime  = 0;

// Double-press tracking
unsigned long pendingPressTime  = 0;
uint8_t       pendingPressCount = 0;

//function prototypes
void transition(AppState newState);
bool buttonPressed();
void clearPendingPress();
int  checkButtonEvent();
void drawMenu();
void drawOutboxIdle();
void drawOutboxReady(const String& msg);
void drawSending(const String& msg);
void addToInbox(const String& msg, int rssi);
void drawInbox();
void loadAESKeyFromNVS();
void saveAESKeyToNVS();
void drawWaitingBT();
void drawConfig();
void drawConfigEditKey();


void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Reset OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Bluetooth - pick a random two-digit suffix so each boot gets a unique name
  randomSeed(esp_random());
  deviceName = "ESP32_OLED_" + String(random(10, 100));

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DI0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  Serial.println("LoRa init succeeded.");

  // BT Start
  SerialBT.begin(deviceName);
  Serial.println("BT started: " + deviceName);

  // Restore AES key saved in NVS (falls back to compiled default if it hasn't been saved yet)
  loadAESKeyFromNVS();

  transition(WAITING_BT);
}

String readBTLine() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n') {
      String line = btBuffer;
      btBuffer = "";
      line.trim();
      return line;
    } else {
      btBuffer += c;
    }
  }
  return "";
}

bool buttonPressed() {
  bool reading = (digitalRead(BUTTON_PIN) == LOW);
  if (reading && lastButtonState == HIGH) {
    unsigned long now = millis();
    if (now - lastDebounceTime > DEBOUNCE_MS) {
      lastDebounceTime = now;
      lastButtonState  = LOW;
      return true;
    }
  }
  if (!reading) lastButtonState = HIGH;
  return false;
}

void clearPendingPress() {
  pendingPressCount = 0;
  pendingPressTime  = 0;
}

// Returns 0 = no event, 1 = single press confirmed, 2 = double press confirmed.
// Single press is confirmed after DOUBLE_PRESS_MS with no follow-up press.
// Double press fires immediately on the second press within the window.
int checkButtonEvent() {
  bool pressed = buttonPressed();
  unsigned long now = millis();

  if (pressed) {
    if (pendingPressCount == 0) {
      pendingPressCount = 1;
      pendingPressTime  = now;
    } else if (pendingPressCount == 1 &&
               (now - pendingPressTime) <= DOUBLE_PRESS_MS) {
      pendingPressCount = 0;
      return 2;  // double press
    }
  }

  // Single press confirmed once the window expires
  if (pendingPressCount == 1 &&
      (millis() - pendingPressTime) > DOUBLE_PRESS_MS) {
    pendingPressCount = 0;
    return 1;  // single press
  }

  return 0;
}

void transition(AppState newState){
  clearPendingPress();
  switch(newState){
    case WAITING_BT:
      drawWaitingBT();
      break;
    case MENU:
      drawMenu();
      break;
    case OUTBOX_IDLE:
      drawOutboxIdle();
      break;
    case OUTBOX_READY:
      drawOutboxReady(messageToSend);
      break;
    case SENDING:
      drawSending(messageToSend);
      break;
    case INBOX:
      drawInbox();
      break;
    case CONFIG:
      drawConfig();
      break;
    case CONFIG_EDIT_KEY:
      drawConfigEditKey();
      break;
  }
}

void sendViaLoRa(const String& msg){
  String encrypted = encryptAES(msg);
  LoRa.beginPacket();
  LoRa.print(encrypted);
  LoRa.endPacket();
  Serial.println("LoRa TX : " + msg);
  Serial.println("LoRa TX (encrypted): " + encrypted);
  SerialBT.println("LoRa TX OK: " + msg);
  SerialBT.println("LoRa TX (encrypted): " + encrypted);
}

void receiveViaLoRa(){
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    Serial.println("LoRa RX: " + incoming);
    String decrypted = decryptAES(incoming);
    Serial.println("LoRa RX (decrypted): " + decrypted);
    SerialBT.println("LoRa RX: " + incoming);
    SerialBT.println("LoRa RX (decrypted): " + decrypted);
    String toStore = decrypted.length() > 0 ? decrypted : "[decrypt failed]";
    addToInbox(toStore, LoRa.packetRssi());
    currentState = INBOX;
    transition(INBOX);
  }
}

void loop(){
  // If BT drops while in any active state, return to the splash screen.
  // WAITING_BT already polls for reconnection, so no extra logic is needed.
  if (currentState != WAITING_BT && !SerialBT.connected()) {
    currentState = WAITING_BT;
    transition(WAITING_BT);
  }

  String btLine  = readBTLine();
  int    btnEvent = checkButtonEvent();  // 0=none, 1=single press, 2=double press

  switch(currentState){
    case WAITING_BT:
      if (SerialBT.connected()) {
        transition(MENU);
        currentState = MENU;
      }
      break;

    case MENU:
      if(btLine == "1"){
        messageToSend = "";
        transition(OUTBOX_IDLE);
        currentState = OUTBOX_IDLE;
      } else if(btLine == "2"){
        transition(INBOX);
        currentState = INBOX;
      } else if(btLine == "3"){
        transition(CONFIG);
        currentState = CONFIG;
      }
      break;

    case OUTBOX_IDLE:
      if(btLine.length() > 0){
        messageToSend = btLine;
        transition(OUTBOX_READY);
        currentState = OUTBOX_READY;
      }
      if(btnEvent >= 1){
        transition(MENU);
        currentState = MENU;
      }
      break;

    case OUTBOX_READY:
      if(btLine.length() > 0){
        messageToSend = btLine;
        drawOutboxReady(messageToSend);
      }
      if(btnEvent >= 1){
        transition(SENDING);
        currentState = SENDING;
        Serial.println("Sending: " + messageToSend);
      }
      break;

    case SENDING:
      sendViaLoRa(messageToSend);
      messageToSend = "";
      currentState = OUTBOX_IDLE;
      transition(OUTBOX_IDLE);
      break;

    case INBOX:
      if (btnEvent == 2 && inboxCount > 1) {
        inboxViewIdx = (inboxViewIdx + 1) % inboxCount;
        drawInbox();
      } else if (btnEvent == 1) {
        transition(MENU);
        currentState = MENU;
      }
      break;

    case CONFIG:
      if(btnEvent == 2){
        // Double press: enter AES key edit mode
        transition(CONFIG_EDIT_KEY);
        currentState = CONFIG_EDIT_KEY;
      } else if(btnEvent == 1){
        transition(MENU);
        currentState = MENU;
      }
      break;

    case CONFIG_EDIT_KEY:
      if(btLine.length() > 0){
        if(btLine.length() == 64 && setAESKeyFromHex(btLine)){
          saveAESKeyToNVS();
          SerialBT.println("AES key updated and saved.");
          SerialBT.println("New key: " + getAESKeyHex());
          Serial.println("AES key updated: " + getAESKeyHex());
          transition(CONFIG);
          currentState = CONFIG;
        } else {
          SerialBT.println("ERROR: expected 64 hex chars, got " +
                           String(btLine.length()) + ". Try again or press btn to cancel.");
        }
      }
      if(btnEvent >= 1){
        transition(CONFIG);
        currentState = CONFIG;
      }
      break;
  }

  //receive LoRa messages
  receiveViaLoRa();
}

// Inbox helpers

void addToInbox(const String& msg, int rssi) {
  // Shift existing entries down, dropping the oldest when the buffer is full
  int newCount = min(inboxCount + 1, INBOX_SIZE);
  for (int i = newCount - 1; i > 0; i--) {
    inboxMsgs[i] = inboxMsgs[i - 1];
    inboxRssi[i] = inboxRssi[i - 1];
  }
  inboxMsgs[0] = msg;
  inboxRssi[0] = rssi;
  inboxCount   = newCount;
  inboxViewIdx = 0;  // always land on the newest message on arrival
}

// NVS helpers

void loadAESKeyFromNVS() {
  prefs.begin("bun1", true);  // read-only
  if (prefs.isKey("aeskey")) {
    prefs.getBytes("aeskey", aesKey, 32);
    Serial.println("AES key loaded from NVS.");
  } else {
    Serial.println("AES key: no saved key found, using compiled default.");
  }
  prefs.end();
}

void saveAESKeyToNVS() {
  prefs.begin("bun1", false);  // read-write
  prefs.putBytes("aeskey", aesKey, 32);
  prefs.end();
  Serial.println("AES key saved to NVS.");
}

// Functions to draw different screens on OLED

void drawWaitingBT() {
  display.clearDisplay();
  display.drawBitmap(0, 0, epd_bitmap_bunnytransmssion, 64, 64, WHITE);
  display.setCursor(50, 10);
  display.println(deviceName);
  display.setCursor(64, 22);
  display.println("Waiting");
  display.setCursor(64, 32);
  display.println("for BT...");
  display.display();
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(deviceName + " MENU:");
  display.println("----------------");
  display.println("1. Send message.");
  display.println("2. Inbox.");
  display.println("3. Configuration.");
  display.println("----------------");
  display.println("Choose (BT):");
  display.display();
}

void drawOutboxIdle() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Enter message:");
  display.println("via BT...");
  display.println();
  display.println("press btn to cancel");
  display.display();
}

void drawOutboxReady(const String& msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("To send:");
  display.println("----------------");
  if (msg.length() > 21) {
    display.println(msg.substring(0, 21));
    display.println(msg.substring(21, 42));
  } else {
    display.println(msg);
  }
  display.println();
  display.println("press button to send");
  display.display();
}

void drawSending(const String& msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sending via LoRa...");
  display.println("----------------");
  display.println(msg.substring(0, 21));
  display.display();
}

void drawConfig() {
  String keyHex = getAESKeyHex();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Configuration:");
  display.print("Freq: ");
  display.print(LORA_FREQUENCY / 1E6);
  display.println(" MHz");
  display.print("BT: ");
  display.println(deviceName);
  display.println("AES key:");
  display.println(keyHex.substring(0, 16));   // bytes  0-7
  display.println(keyHex.substring(16, 32));  // bytes 8-15
  display.println("btn=back");
  display.println("dbl=edit AES key");
  display.display();
}

void drawConfigEditKey() {
  String keyHex = getAESKeyHex();
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Edit AES Key:");
  display.println("Send 64 hex chars");
  display.println("via BT.");
  display.println("Current key:");
  display.println(keyHex.substring(0, 16));   // bytes  0-7
  display.println(keyHex.substring(16, 32));  // bytes 8-15
  display.println("...");
  display.println("btn=cancel");
  display.display();
}

void drawInbox() {
  display.clearDisplay();
  display.setCursor(0, 0);
  if (inboxCount == 0) {
    display.println("Inbox:");
    display.println("----------------");
    display.println("No messages yet.");
    display.println();
    display.println("btn=back");
  } else {
    const String& msg = inboxMsgs[inboxViewIdx];
    display.print("Inbox ");
    display.print(inboxViewIdx + 1);
    display.print("/");
    display.println(inboxCount);
    display.println("----------------");
    if (msg.length() > 21) {
      display.println(msg.substring(0, 21));
      display.println(msg.substring(21, 42));
    } else {
      display.println(msg);
    }
    display.print("RSSI: ");
    display.print(inboxRssi[inboxViewIdx]);
    display.println(" dBm");
    display.println();
    if (inboxCount > 1) {
      display.println("btn=back dbl=next");
    } else {
      display.println("btn=back");
    }
  }
  display.display();
}
