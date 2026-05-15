// Bluetooth libraries
#include <BluetoothSerial.h>
#include <string.h>

// OLED libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <graphics.h>

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

//state machine states
enum AppState {
  MENU,
  OUTBOX_IDLE,
  OUTBOX_READY,
  SENDING,
  INBOX,
  CONFIG
};

AppState currentState = MENU;

//auxiliary variables
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
BluetoothSerial SerialBT;

// Global variables
const String deviceName = "ESP32_OLED-2";

String btBuffer      = "";   // buffer for incoming BT data
String messageToSend = "";   // message waiting in outbox to be sent via LoRa
String lastReceived  = "";   // last received message via LoRa (for display in inbox)

bool lastButtonState   = HIGH;
unsigned long lastDebounceTime = 0;

//function prototypes
void transition(AppState newState);
bool buttonPressed();
void drawMenu();
void drawOutboxIdle();
void drawOutboxReady(const String& msg);
void drawSending(const String& msg);
void drawInbox();
void drawConfig();


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

  // Ekran powitalny
  display.drawBitmap(0, 0, epd_bitmap_bunnytransmssion, 64, 64, WHITE);
  display.setCursor(64, 10);
  display.println("Waiting");
  display.setCursor(64, 18);
  display.println("for BT...");
  display.display();

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DI0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  Serial.println("LoRa init succeeded.");

  // Bluetooth
  SerialBT.begin(deviceName);
  Serial.println("BT started: " + deviceName);

  transition(MENU);
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

void transition(AppState newState){
  switch(newState){
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
  }
}

void sendViaLoRa(const String& msg){
  LoRa.beginPacket();
  LoRa.print(msg);
  LoRa.endPacket();
  Serial.println("LoRa TX: " + msg);
  SerialBT.println("LoRa TX OK: " + msg);
}

void receiveViaLoRa(){
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    Serial.println("LoRa RX: " + incoming);
    SerialBT.println("LoRa RX: " + incoming);
    lastReceived = incoming;
  }
}

void loop(){
  String btLine = readBTLine();
  bool btnState = buttonPressed();

  switch(currentState){
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
      if(btnState){
        transition(MENU);
        currentState = MENU;
      }
      break;

    case OUTBOX_READY:
      if(btLine.length() > 0){
        messageToSend = btLine;
        drawOutboxReady(messageToSend);
      }
      if(btnState){
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
      if(btnState){
        transition(MENU);
        currentState = MENU;
      }
      break;
    case CONFIG:
      if(btnState){
        transition(MENU);
        currentState = MENU;
      }
      break;
  }

  //recive LoRa messages
  receiveViaLoRa();
}

// Functions to draw different screens on OLED

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("MENU:");
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
  display.println("press button to cancel");
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
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Configuration:");
  display.println("----------------");
  display.print("Frequency: ");
  display.print(LORA_FREQUENCY / 1E6);
  display.println(" MHz");
  display.print("BT: ");
  display.println(deviceName);
  display.println("----------------");
  display.println("press button to go back");
  display.display();
}

void drawInbox() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Inbox:");
  display.println("----------------");
  if (lastReceived.length() == 0) {
    display.println("No messages yet.");
  } else {
    if (lastReceived.length() > 21) {
      display.println(lastReceived.substring(0, 21));
      display.println(lastReceived.substring(21, 42));
    } else {
      display.println(lastReceived);
    }
  }
  display.println();
  display.println("press button to go back");
  display.display();
}
