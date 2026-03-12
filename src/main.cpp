//OLED libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <graphics.h>

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

void setup() {
  Serial.begin(115200);
  

  //reset OLED display via software
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  //initialize OLED and display buffers
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false);
  display.setTextSize(1);
  display.setTextWrap(1);
  display.setTextColor(WHITE);

  display.clearDisplay();
  display.clearDisplay();
  
}

uint32_t iter = 0;

void loop() {
  if (iter==192){
    iter=0;
  }
  display.clearDisplay();
  display.drawBitmap(iter-64, 0, epd_bitmap_bunnytransmssion, 64, 64, WHITE);

  display.setCursor(16,28);
  display.print("Radio Transmission");

  display.display();
  iter+=2;
}
