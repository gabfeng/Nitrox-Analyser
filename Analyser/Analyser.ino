#include <ESP8266WiFi.h>

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ADS1115_config.h>
#include <ADS1115_WE.h>
#include <string.h>

#include <RBD_Timer.h>
#include <U8x8lib.h>

#define LOG 0
#define ECHOPIN 15 // D8
#define TRIGGERPIN 2 // D4
#define SENSORHZ 3
#define SCROLLHZ 2
#define ADSADDR 0x49
#define BUTTONPIN 0 // D3
#define SHORTPRESSTIME 500
#define SHORTPRESS 1
#define LONGPRESS 2
#define NOPRESS 0
#define PRESSING -1
#define LONGPRESSING -2

// #ifdef U8X8_HAVE_HW_SPI
// #include <SPI.h>
// #endif

/**
 *
 * ~15.85mm each round. ~41.70mm outside edge to outside edge.
 *
 */

int scrollIndex = 0;
String errorTxt = "";

ADS1115_WE adc = ADS1115_WE(ADSADDR); // Default 0x48 ADDR

RBD::Timer sensorTimer;
RBD::Timer scrollTimer;
RBD::Timer menuTimer;

// U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8();
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
// U8X8_SH1106_128X64_NONAME_4W_HW_SPI u8x8(/* cs=*/8, /* dc=*/0); //(/* cs=*/10, /* dc=*/0, /*reset=*/8);
bool bigDisplay = true;

// Button variables
int buttonPress = NOPRESS;
int lastState = HIGH; // the previous state from the input pin
int currentState;    // the current reading from the input pin
unsigned long pressedTime = 0;
unsigned long releasedTime = 0;

// EEPROM Variables
int addr_calibratedmv_Air = sizeof(float);
int addr_calibratedTime_us = sizeof(long);
int addr_calibratedmV_O2 = sizeof(float);
int addr_calibratedmV_Zero = sizeof(float);
int addr_flip = sizeof(bool);
int addrList[5] = {addr_calibratedmv_Air, addr_calibratedTime_us, addr_calibratedmV_O2, addr_calibratedmV_Zero, addr_flip};

float calibratedmV_Air = 0;        // 0
long calibratedTime_us = 0;        // 1
float calibratedmV_O2 = 0;             // 2
float calibratedmV_Zero = 0;           // 3
bool flip = false;                      // 4
bool calibrated_Air = 0;
bool calibrated_O2 = 0;

int sos_O2 = 329;
int sos_N2 = 352;
int sos_He = 1014;
float o2pct = 0;

// Menu
const char *menu[6] = {"Flip", "Details", "1Cal", "2Cal", "3Cal"};
String menuLine = "";
int menuIndex = -1;
bool calibrating = false;
bool frozen = false;
bool highlightingMenu = false;


void staticText(void)
{
  uint8_t cols = u8x8.getCols();
  uint8_t rows = u8x8.getRows();

  u8x8.clear();
  if (bigDisplay)
  {
    u8x8.setFont(u8x8_font_8x13_1x2_r);

    u8x8.draw2x2String(0, 0, "O");
    u8x8.draw2x2String(0, 4, "H");

    u8x8.setFont(u8x8_font_5x7_r);
    u8x8.drawString(2, 1, "2");
    u8x8.drawString(2, 54, "e");

    u8x8.setFont(u8x8_font_8x13_1x2_r);
  }
  else
  {
    u8x8.setFont(u8x8_font_5x8_r);

    u8x8.drawString(0, 1, "O");
    u8x8.drawString(0, 2, "2");

    u8x8.drawString(cols / 2, 1, "H");
    u8x8.drawString(cols / 2, 2, "e");
    u8x8.drawString(0, 3, "MOD");
  }
}

void scrollText(const char *s)
{
  u8x8.setFont(u8x8_font_5x7_r);

  u8x8.clearLine(6);
  if (scrollIndex == strlen(s))
  {
    scrollIndex = 0;
  }

  u8x8.drawString(scrollIndex, 6, s);
  scrollIndex--;
}

void printConfigReg(const char *msg, bool conv)
{
  // uint16_t reg = conv ? adc.readRegister(adc.ADS1115_CONV_REG) : adc.readRegister(adc.ADS1115_CONFIG_REG);
  // Serial.println(msg);
  // for (int i = 0; i < 16; i++)
  // {
  //   Serial.print(i);
  //   Serial.print(":");
  //   Serial.print(reg >> i & 1, BIN);
  //   Serial.print("   ");
  // }
  // Serial.println("");
}

float getO2mv()
{
  return adc.getResult_mV(); // for Millivolt
  // return 0;
}

long getUltrasonicDuration()
{
  digitalWrite(TRIGGERPIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGERPIN, LOW);
  unsigned long duration_us = pulseIn(ECHOPIN, HIGH, 38500);
  if (duration_us >= 38000)
  {
    Serial.print("Out of Range");
  }
    Serial.print("Time: ");
    Serial.println(duration_us);

  return static_cast<long> (duration_us);
}

void showCalibration()
{
  if((menuIndex == 3 && !calibrated_Air) || (menuIndex == 4 && (!calibrated_Air || !calibrated_O2) ))
  {

    u8x8.setFont(u8x8_font_5x7_r);
    u8x8.drawString(0, u8x8.getRows()/2 -1, "Calibration Order:");
    u8x8.drawString(0, u8x8.getRows()/2, "   Air -> O2 -> He");

    delay(1500);
    hideMenu();
  }
  else
  { 
    if(!calibrating)
    {
      u8x8.clear();
      u8x8.setFont(u8x8_font_8x13_1x2_r);
      u8x8.drawString(0, 0, "O2:");
      u8x8.drawString(0, 2, "T:");
      u8x8.drawString(0, 4, "L:");
      
      u8x8.setFont(u8x8_font_5x7_r);
      u8x8.setInverseFont(1);
      u8x8.drawString(0, u8x8.getRows()-1, "Press to cal");
      u8x8.setInverseFont(0);
      calibrating = true;
    }

    float o2mV = getO2mv();
    String o2mV_str = (o2mV < 10 ? "0" : "") + String(o2mV, 2)+"mV";
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.drawString(4, 0, o2mV_str.c_str());
  
    long duration_us = getUltrasonicDuration();
    
    String dur_us_str = String(duration_us) + "us";
    if(duration_us < 1000 )
    {
      dur_us_str = "0" + dur_us_str;
    }
    if(duration_us < 10000 )
    {
      dur_us_str = "0" + dur_us_str;
    }
    float airSpeed = (sos_O2 * 0.21 + sos_N2 * 0.79);
    float calibratedLength = duration_us * airSpeed / 10000;
    String calLength_str = String(calibratedLength, 2)+"cm";
    u8x8.drawString(4, 2, dur_us_str.c_str());
    u8x8.drawString(4, 4, calLength_str.c_str());
  }
}

void firstPointCalibration()
{
  calibratedmV_Air = getO2mv();
  EEPROM.put(0, calibratedmV_Air);
  #ifdef LOG
  Serial.print("calibratedmV_Air: ");
  Serial.println(calibratedmV_Air);
  #endif

  unsigned long duration_us = getUltrasonicDuration();
  if (duration_us < 38000)
  {
    calibratedTime_us = duration_us;
    EEPROM.put(addrList[0], calibratedTime_us);
    float airSpeed = (sos_O2 * 0.21 + sos_N2 * 0.79);
    float calibratedLength = calibratedTime_us * airSpeed;
    #ifdef LOG
    Serial.print("calibratedTime_us: ");
    Serial.println(calibratedTime_us);
    Serial.print("calibratedLength: ");
    Serial.println(calibratedLength);
    #endif
  }

  calibratedmV_O2 = 0;
  EEPROM.put(addrList[0] + addrList[1], calibratedmV_O2);
  calibratedmV_Zero = 0;
  EEPROM.put(addrList[0] + addrList[1] + addrList[2], calibratedmV_Zero);

  calibrated_Air = true;
  

}

void secondPointCalibration()
{
  calibratedmV_O2 = getO2mv();
  EEPROM.put(addrList[0] + addrList[1], calibratedmV_O2);
}

void thirdPointCalibration()
{
  calibratedmV_Zero = getO2mv();
  EEPROM.put(addrList[0] + addrList[1] + addrList[2], calibratedmV_Zero);
}

void confirmCalibration() 
{
  if(menuIndex == 2)
  {
    firstPointCalibration();
  }
  else if(menuIndex == 3)
  {
    secondPointCalibration();
  }
  else if(menuIndex == 4)
  {
    thirdPointCalibration();
  }

  u8x8.clear();
  
  u8x8.setFont(u8x8_font_8x13_1x2_r);
  u8x8.drawString(0, u8x8.getRows()/2 - 1, "Calibrated!");

  calibrating = false;

  delay(1000);
  hideMenu();
}

float getO2()
{
  float mv = getO2mv();
  o2pct = (0.2095 * mv) / calibratedmV_Air;
  return o2pct;
}

void displayO2()
{
  float o2 = getO2();
  Serial.print("O2: ");
  Serial.println(o2,3);
  String o2_str = String(o2 * 100, 2);
  if (bigDisplay)
  {
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.draw2x2String(4, 0, o2_str.c_str());
  }
  else
  {
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.draw1x2String(2, 0, o2_str.c_str());
  }
}

void displayHe()
{
  long duration_us = getUltrasonicDuration();
  float he = 0;
  // Only update display if new duration is more than calibrated, errors in ultrasonic, how to better handle instead of not showing???
  if(duration_us <=  calibratedTime_us){
    
    float airSpeed = (sos_O2 * 0.21 + sos_N2 * 0.79);
    // Serial.print("airSpeed: ");
    // Serial.println(airSpeed);
    float calibratedLength = calibratedTime_us * airSpeed;
  
    float mixSpeed = calibratedLength / duration_us;
    // Serial.print("mixSpeed: ");
    // Serial.println(mixSpeed);
    
    float z = o2pct;
    int a = sos_N2;
    int b = sos_He;
    int c = sos_O2;
    float d = mixSpeed;
  
    // long y = (-a * x + c * (x - 100) + d)/(b - c); // and z = -(-a x + b (x - 100) + d)/(b - c) and b!=c
    float x = (((-d + (c * z) + a - (b * z)) / (b - a)) + 1);
    // Serial.print("x: ");
    // Serial.println(x);
    he = 1-x;
    
    if(he < 0){
      he = 0;
    }
  
  }

  String he_str = (he<0.1 ? "0" : "") + String(he * 100, 2);
  Serial.print("He: ");
  Serial.println(he);
  // Serial.print("HeStr: ");
  // Serial.println(he_str.c_str());

  if (bigDisplay)
  {
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    if(he<0.1)
    {

    }
    u8x8.draw2x2String(3, 4, he_str.c_str());
  }
  else
  {
    u8x8.setFont(u8x8_font_8x13_1x2_r);
    u8x8.draw1x2String(2, 0, he_str.c_str());
  }
}

// void checkButtonPress() {
  
// }

void checkButtonPress()
{
  currentState = digitalRead(BUTTONPIN);

  if (lastState == HIGH && currentState == LOW) // button is pressed
  {
     pressedTime = millis();
     buttonPress = PRESSING;
  }
  else if (lastState == LOW && currentState == HIGH) // button is released
  { 
    releasedTime = millis();

    long pressDuration = releasedTime - pressedTime;

    if (pressDuration < SHORTPRESSTIME)
    {
      buttonPress = SHORTPRESS;
    }
    else
    {
      buttonPress = LONGPRESS;
    }
  }
  else if(lastState == LOW && currentState == LOW) // Still pressing
  {
    unsigned long now = millis();
    long pressDuration = now - pressedTime;

     if(pressDuration >= SHORTPRESSTIME)
     {
       buttonPress = LONGPRESSING;
     }
  }
  else 
  {
    buttonPress = NOPRESS;
  }

  // save the the last state
  lastState = currentState;
// Serial.print("pressed ");
// Serial.println(buttonPress);
// Serial.print("currentState ");
// Serial.println(currentState);

}

void flipScreen()
{
  flip = !flip;
  u8x8.clear();
  u8x8.setFlipMode(flip ? 1 : 0);
  
  EEPROM.put(addrList[0] + addrList[1] + addrList[2] + addrList[3] + addrList[4], flip);
}

void freezeScreen(bool freeze)
{
  if (freeze)
  {
    sensorTimer.stop();
    scrollTimer.stop();

    u8x8.drawString(u8x8.getCols()-1,u8x8.getRows()-1, "F");    
    frozen = true;
  }
  else
  {
    sensorTimer.restart();
    scrollTimer.restart();
    uint8_t tiles[8] = {0,0,0,0,0,0,0,0};
    u8x8.drawTile(1, 3, 1, tiles); // Clear the 'F' ?
    frozen = false;
  }
}

void hideMenu()
{
  Serial.println("Hide Menu");
  u8x8.clear();
  menuIndex = -1;
  highlightingMenu = false;
  sensorTimer.restart();
  staticText();
}

void showMenu()
{
  Serial.println("Show Menu");
  sensorTimer.stop();
  u8x8.clear();
  
  u8x8.setFont(u8x8_font_5x7_r);

  for(int i = 0; i < sizeof(menu) / sizeof(const char*); i++)
  {
    u8x8.drawString(0, i, menu[i]);
  }

  updateMenu();
}

void updateMenu()
{
  Serial.println("Update Menu");
  menuTimer.restart();

  int menuLen = sizeof(menu) / sizeof(const char*);

  if(buttonPress == SHORTPRESS)
  {
    if(menuIndex < menuLen - 1)
    {
      menuIndex++;
    }
    else {
      menuIndex = 0;
      // Redraw previous menu item (last line) without the >
      u8x8.clearLine(menuLen - 1);
      u8x8.drawString(0, menuLen - 1, menu[menuLen-1]);
    }
    // Serial.println("Menu: " + String(menuIndex));
    u8x8.setFont(u8x8_font_5x7_r);
  
    if(menuIndex > 0)
    {
      // Redraw previous menu item without the >
      u8x8.clearLine(menuIndex - 1);
      u8x8.drawString(0, menuIndex - 1, menu[menuIndex-1]);
    }
  }

// Will the highlighted line flicker?
  if((buttonPress == LONGPRESSING && !highlightingMenu) || buttonPress == SHORTPRESS)
  {
    if(buttonPress == LONGPRESSING)
    {
      u8x8.setInverseFont(1);
      highlightingMenu = true;
    }
    u8x8.clearLine(menuIndex);
    u8x8.drawString(0, menuIndex, ">");
    u8x8.drawString(1, menuIndex, menu[menuIndex]);
    u8x8.setInverseFont(0);

  }
}

void selectMenu()
{
  highlightingMenu = false;
  menuTimer.stop();
  switch (menuIndex)
  {
    case 0: // Flip
      flipScreen();
      hideMenu();
      break;
    // case 1: // Freeze
    //   freezeScreen(true);
    //   break;
    case 2:
    case 3:
    case 4:
      showCalibration();
      break;
    default:
      break;
  }
  menuIndex = -1;
}

// the setup function runs once when you press reset or power the board
void setup()
{
  // Comms
  Serial.begin(115200);
  WiFi.mode(WIFI_OFF);
  Serial.println("Setup");
  Wire.begin();

  // Load from EEPROM
  EEPROM.get(0, calibratedmV_Air);
  EEPROM.get(addrList[0], calibratedTime_us);
  EEPROM.get(addrList[0] + addrList[1], calibratedmV_O2);
  EEPROM.get(addrList[0] + addrList[1] + addrList[2], calibratedmV_Zero);
  EEPROM.get(addrList[0] + addrList[1] + addrList[2] + addrList[3] + addrList[4], flip);
  Serial.println("Calibrated values");
  Serial.println(("Air mV: " + String(calibratedmV_Air,2)).c_str());
  Serial.println(("Return Time: " + String(calibratedTime_us)).c_str());
  Serial.println(("O2 mV: " + String(calibratedmV_O2,2)).c_str());
  Serial.println(("Zero mV: " + String(calibratedmV_Zero,2)).c_str());

  // Display setup
  u8x8.begin();
  u8x8.setFlipMode(flip ? 1 : 0);

  // ADS setup
  // analogReference(INTERNAL); // Set internal voltage reference to 1.1v
  if (!adc.init())
  {
    errorTxt = "ADS1115: Failed to connect";
    Serial.println("ADS1115: Failed to connect");
  }
  else
  {
    adc.reset();
    adc.setVoltageRange_mV(ADS1115_RANGE_0256);
    adc.setConvRate(ADS1115_8_SPS);
    adc.setMeasureMode(ADS1115_CONTINUOUS);
    adc.setCompareChannels(ADS1115_COMP_0_1);
  }

  // Ultrasonic setup
  pinMode(TRIGGERPIN, OUTPUT);
  digitalWrite(TRIGGERPIN, LOW);
  delayMicroseconds(2);
  pinMode(ECHOPIN, INPUT);

  pinMode(BUTTONPIN, INPUT_PULLUP);

  // Sensor timing
  sensorTimer.setHertz(SENSORHZ);
  sensorTimer.restart();

  // Scroll timing
  // scrollTimer.setHertz(SCROLLHZ);
  // scrollTimer.restart();

  menuTimer.setTimeout(3000);

  // Menu
  for (String m : menu)
  {
    if (menuLine != "")
    {
      menuLine = menuLine + " ";
    }
    menuLine = menuLine + m;
  }

  staticText();
}

void loop(void)
{
  bool stoppedSensorTimer = sensorTimer.isStopped();

  // checkButtonPress();

  // Normal display
  if (!calibrating && menuIndex == -1 && !stoppedSensorTimer)
  {
    if(sensorTimer.onRestart())
    {
      displayO2();
      displayHe();
    }
    if(buttonPress == SHORTPRESS)
    {
      showMenu();
    }
  }
  else 
  {
    if(menuIndex >=0) // In Menu
    {
      if(buttonPress == SHORTPRESS || buttonPress == LONGPRESSING)
      {
        updateMenu();
      }
      else if(buttonPress == LONGPRESS)
      {
        selectMenu();
      }
    }
    else if(calibrating)
    {
      showCalibration();
      if(buttonPress == SHORTPRESS || buttonPress == LONGPRESS)
      {
        confirmCalibration();
      }
    }
    
    if(menuTimer.onExpired())
    {
      hideMenu();
    }
  }
 
  // if(buttonPress != PRESSING && buttonPress != LONGPRESSING)
  // {
  //   if(!calibrating)
  //   {
  //     if (buttonPress == SHORTPRESS)
  //     {
  //       if(menuIndex == -1)
  //       {
  //         showMenu();
  //     Serial.println("3");
  //       }
  //       else 
  //       {
  //         updateMenu();
  //     Serial.println("4");
  //       }
        
  //     }
  //     else if (buttonPress == LONGPRESS)
  //     {
  //       selectMenu();
  //     }
  //   }
  //   else
  //   {
  //     showCalibration();
  //     if(buttonPress == SHORTPRESS || buttonPress == LONGPRESS)
  //     {
  //       confirmCalibration();
  //     }
  //   }
   
  //   if(menuTimer.onExpired())
  //   {
  //     hideMenu();
  //   }
  // }

  // if (!bigDisplay && scrollTimer.onRestart())
  // {
  //   scrollText("This is a long string.");
  // }
}
