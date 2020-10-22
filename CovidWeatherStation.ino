
#include <Adafruit_Sensor.h>        // https://github.com/adafruit/Adafruit_Sensor Version 1.1.4
#include <Adafruit_BME280.h>        // https://github.com/adafruit/Adafruit_BME280_Library Version 2.1.0
#include <SparkFun_APDS9960.h>      // https://github.com/sparkfun/SparkFun_APDS-9960_Sensor_Arduino_Library Version 1.4.2
#include <esp32_digital_led_lib.h>  // https://github.com/MartyMacGyver/ESP32-Digital-RGB-LED-Drivers Version 1.5.3

#include <Preferences.h>
#include <WiFi.h>
#include <Esp32WifiManager.h>

#include "corona_information.h"

//Create a wifi manager
WifiManager manager;

// Declare  strip object: 3x 20 LEDs
#define NumPix1    20
#define NumPix2    20
#define NumPix3    20
#define NumPix  (NumPix1 + NumPix2 + NumPix3)
strand_t stripe = {.rmtChannel = 0, .gpioNum = 12, .ledType = LED_WS2812B_V1, .brightLimit = 35,
                   .numPixels = NumPix,
                   .pixels = nullptr, ._stateVars = nullptr
                  };
strand_t* stripes[] = { &stripe };

// ColorSettings for enlighted and non-shining pixels
pixelColor_t activeColor ;                                // the currently active color
pixelColor_t defaultColor =   pixelFromRGB(200, 100, 52); // Color for Sensor values in standard Mode
pixelColor_t refRange = pixelFromRGB(8, 15, 45);          // Color for marking the reference comfort area
pixelColor_t historyColor =   pixelFromRGB(150, 20, 20);  // Color for Sensor values in history Mode
pixelColor_t off = pixelFromRGB(0, 0, 0);
pixelColor_t dimmedColor;
pixelColor_t dimmedColor50Percent;


struct Gesture {
  const uint8_t PIN;
  uint32_t eventsCounter;
  bool active;
};

struct ClimaData {
  int temp;   // in 1/10 °C   215 = 21.5°C
  int rH;     // in %
  int p;      // in Pa
  int pTrendTwoHours; //in Pa
  uint16_t brightness;  // 0... 37889
};

int dimmPercent;

struct Timer {
  const unsigned long intervall;
  unsigned long previous;
};


unsigned long nextHttpCall = 0;
unsigned long nextHmiSwitch = 0;
uint8_t currentHmi = 0;

Adafruit_BME280 bme; // I2C
SparkFun_APDS9960 apds = SparkFun_APDS9960();

#define APDS9960_INT    18  //  Interrupt pin
int isr_flag = 0;
Gesture gestureAPDS = {APDS9960_INT, 0, false};
ClimaData clima = {0 , 0, 0, 0};

#define historyPoints   288    // 288 Points at 5 min refresh rate are equal to 1 day!
ClimaData climaHistory[historyPoints];
int historyPointsCount;

Timer timerClima = {2000, 0};  // how often data are read from sensor {ms}
Timer timerLED = {20, 0};      // how often the LED pixels are refreshed
Timer timerHistory = {300000, 0}; //  1/ 5min
Timer timerBlink = {800, 0};   // blinking intervall in case of data reange exceeding
Timer timerTrendBlink = {3000, 0}; // how often the pTrend Animation is triggered
Timer timerTrendAnimation = {60, 0}; // Animation frame length

int TrendAnimationPos = 0;
boolean animationOn = false;
boolean blinkingOn = false;
int blinkBrightness = 0;

void IRAM_ATTR isr() {
  isr_flag = 1;
  //gestureAPDS.eventsCounter += 1;
  //gestureAPDS.active = true;
  //Serial.print("Interrupt!  ");
  //Serial.println(gestureAPDS.eventsCounter);
  //Serial.println(apds.readGesture()); does lead to crash!!!

}

int viewNumber = 0;


void setup() {
  Serial.begin(115200);


  // BME-280
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  // ADPS-9960
  pinMode(gestureAPDS.PIN, INPUT_PULLUP);
  attachInterrupt(gestureAPDS.PIN, isr, FALLING);
  // Initialize APDS-9960 (configure I2C and initial values)
  if ( apds.init() ) {
    Serial.println(F("APDS-9960 initialization complete"));
  } else {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }

  if ( apds.enableLightSensor(false) ) {
    Serial.println(F("Light sensor is now running"));
  } else {
    Serial.println(F("Something went wrong during light sensor init!"));
  }

  /* --------------------------------------------------------------------------------------
      Gesture sensor interrupt handling does not work correctly for Jochen. Who can help?
     --------------------------------------------------------------------------------------
  */
  /*
    apds.setGestureGain(GGAIN_1X); // ohne diese Zeile geht es nicht zuverlässig
       if ( apds.enableGestureSensor(true) ) {
     Serial.println(F("Gesture sensor is now running"));
    } else {
     Serial.println(F("Something went wrong during gesture sensor init!"));
    }
  */

  // LED Stripe
  if (digitalLeds_initDriver()) {
    Serial.println("Init FAILURE: halting");
    while (true) {};
  }
  if (digitalLeds_addStrands((strand_t**) &stripes, 1)) {
    Serial.println("Init FAILURE: halting");
    while (true) {};
  }
  digitalLeds_resetPixels((strand_t**) &stripes, 1);
  activeColor = defaultColor;
  // Start Animation
  stripe.pixels[0] = pixelFromRGB(15, 2, 0);
  stripe.pixels[20] = pixelFromRGB(15, 2, 0);
  stripe.pixels[40] = pixelFromRGB(15, 2, 0);
  digitalLeds_drawPixels((strand_t**) &stripes, 1);
  
  //manager.erase();// This will erase the stored passwords
  manager.setupScan();
  setup_corona_api(8);
  
  delay(1000);
  stripe.pixels[1] = pixelFromRGB(30, 5, 0);
  stripe.pixels[21] = pixelFromRGB(30, 5, 0);
  stripe.pixels[41] = pixelFromRGB(30, 5, 0);
  digitalLeds_drawPixels((strand_t**) &stripes, 1);
  delay(1000);
  /* Wait until first Hmi Switch for 10s */
  nextHmiSwitch = millis() + 10000ul;
}

void loop() {

  // Currently not active
  //if (gestureAPDS.active)    handleGesture();

  unsigned long currentMillis = millis();
  // read the serial port for new passwords and maintain connections
  manager.loop();
  if (manager.getState() == Connected) {
    if (nextHttpCall <= currentMillis) {
      makehttpRequest();
      /* Schedule next call within one hour */
      nextHttpCall = currentMillis + 3600000ul;
      Serial.print("Inzidenz: ");
      Serial.println(get_inzidenz_value());
    }
  }

  // Update clima
  if (currentMillis - timerClima.previous >= timerClima.intervall) {
    timerClima.previous = currentMillis;

    readClimaSensors();
    //Serial.println(clima.p);
      }
    if (currentHmi == 0) {
      // Update LEDs
      if (currentMillis - timerLED.previous >= timerLED.intervall) {
        timerLED.previous = currentMillis;
        updateLEDs(clima);
      }
    
      // BlinkTimer
      if (currentMillis - timerBlink.previous >= timerBlink.intervall) {
        timerBlink.previous = currentMillis;
        blinkingOn = !blinkingOn;
      }
    
      // AnimationTriggering
      if (currentMillis - timerTrendBlink.previous >= timerTrendBlink.intervall) {
        timerTrendBlink.previous = currentMillis;
        animationOn = !animationOn;
    
        if (!animationOn) {
    
          TrendAnimationPos = -1;
        }
        else {
          if (climaHistory[0].pTrendTwoHours < -10) {
            TrendAnimationPos = 430;
            if (climaHistory[0].pTrendTwoHours < -100) TrendAnimationPos = 440;
            if (climaHistory[0].pTrendTwoHours < -150) TrendAnimationPos = 450;
            if (climaHistory[0].pTrendTwoHours < -200) TrendAnimationPos = 460;
          }
          if (climaHistory[0].pTrendTwoHours > 10) {
            TrendAnimationPos = 430;
            if (climaHistory[0].pTrendTwoHours > 100) TrendAnimationPos = 420;
            if (climaHistory[0].pTrendTwoHours > 150) TrendAnimationPos = 410;
            if (climaHistory[0].pTrendTwoHours > 200) TrendAnimationPos = 400;
          }
        }
      }
    
      // TrendAnimation for pressure changes
    
      if (currentMillis - timerTrendAnimation.previous >= timerTrendAnimation.intervall) {
        timerTrendAnimation.previous = currentMillis;
        
        if (animationOn) {
          if (climaHistory[0].pTrendTwoHours < -10) {
            TrendAnimationPos -= 5;
            if (TrendAnimationPos < 400) {
              TrendAnimationPos = -1;
              animationOn = false;
            }
          }
          if (climaHistory[0].pTrendTwoHours > 10) {
            TrendAnimationPos += 5;
            if (TrendAnimationPos > 460) {
              TrendAnimationPos = -1;
              animationOn = false;
            }
          }
        }
      }
    
    
    
      // Update History
      if (currentMillis - timerHistory.previous >= timerHistory.intervall) {
        timerHistory.previous = currentMillis;
    
        // Change LED color
        activeColor =   historyColor;
    
        updateHistory();
    
        // Back to default color...
        activeColor =   defaultColor;
      }
    }
    else { /* currentHmi == 1 */
        updateCovidLEDs(get_inzidenz_value());
    }

    if (nextHmiSwitch <= currentMillis) {
      nextHmiSwitch = currentMillis + 10000ul;
      switch (currentHmi) {
        case 0:
          currentHmi = 1;
          break;
        default:
          currentHmi = 0;
          break;
      }
    }
  }

void readClimaSensors() {
  clima.temp = bme.readTemperature() * 10;
  clima.rH = bme.readHumidity();
  clima.p = bme.readPressure();
  apds.readAmbientLight(clima.brightness);
}

void updateCovidLEDs(uint16_t inzidenz) {

  for (int i = 0; i <= 60; i++)     stripe.pixels[i] = off;

  // Define dimming ( brightness ) of LEDs
  uint16_t brightness;
  apds.readAmbientLight(brightness);

  dimmPercent = max(2, (int)(0.95 * dimmPercent + 0.05 * min(100, brightness * 100 / 4000))); // 4000 is set as max brightness

  // LightDot Position
  dimmedColor.r = activeColor.r * dimmPercent / 100;
  dimmedColor.g = activeColor.g * dimmPercent / 100;
  dimmedColor.b = activeColor.b * dimmPercent / 100;
  dimmedColor50Percent.r = dimmedColor.r / 2;
  dimmedColor50Percent.g = dimmedColor.g / 2;
  dimmedColor50Percent.b = dimmedColor.b / 2;

  //  Structure of values to be handed over
  //         sensorval, (pixelrange), (valueLimits), (refRange), dimming in %

  if (inzidenz < 27) {
  // Temperature
  setLEDPixels(inzidenz, 1, 19, 1, 27, 1, 27, dimmPercent);
  } else if (inzidenz < 70) {
  // rel. humidty
  setLEDPixels(inzidenz, 21, 39, 27, 70, 35, 50, dimmPercent);
  } else {
  // pressure
  setLEDPixels(inzidenz, 41, 59, 70, 105, 70, 80, dimmPercent);
  }
  digitalLeds_drawPixels((strand_t**) &stripes, 1);
}

void updateLEDs(ClimaData climaDateSet) {

  for (int i = 0; i <= 60; i++)     stripe.pixels[i] = off;

  // Define dimming ( brightness ) of LEDs
  uint16_t brightness;
  apds.readAmbientLight(brightness);

  dimmPercent = max(2, (int)(0.95 * dimmPercent + 0.05 * min(100, brightness * 100 / 4000))); // 4000 is set as max brightness

  // LightDot Position
  dimmedColor.r = activeColor.r * dimmPercent / 100;
  dimmedColor.g = activeColor.g * dimmPercent / 100;
  dimmedColor.b = activeColor.b * dimmPercent / 100;
  dimmedColor50Percent.r = dimmedColor.r / 2;
  dimmedColor50Percent.g = dimmedColor.g / 2;
  dimmedColor50Percent.b = dimmedColor.b / 2;

  //  Structure of values to be handed over
  //         sensorval, (pixelrange), (valueLimits), (refRange), dimming in %

  // Temperature
  setLEDPixels(climaDateSet.temp, 1, 19, 160, 255, 200, 220, dimmPercent);
  // rel. humidty
  setLEDPixels(climaDateSet.rH, 21, 39, 20, 67, 40, 55, dimmPercent);
  // pressure
  setLEDPixels(climaDateSet.p, 41, 59, 86000, 105000, 96000, 100000, dimmPercent);

  // pressure Trend (with a 50% inerpolation) TrendAnimationPos is caled with factor 10
  //   420 --> Pixel 42
  //  425 --> Pixel 42 and 43 both with 50%

  if (TrendAnimationPos > 0) {

    if ((TrendAnimationPos % 10) == 5) {
      stripe.pixels[TrendAnimationPos / 10 ] = dimmedColor50Percent;
      stripe.pixels[TrendAnimationPos / 10 + 1 ] = dimmedColor50Percent;
    }
    else {
      stripe.pixels[TrendAnimationPos / 10 ] = dimmedColor;
    }
  }
  digitalLeds_drawPixels((strand_t**) &stripes, 1);
}

void setLEDPixels(int climaValue, int minLED, int maxLED, int minValue, int maxValue, int refLSL, int refUSL, int dimmPercent) {
  int inUpperLimit = 0;
  int inLowerLimit = 0;
  pixelColor_t dimmedRefColor;
  pixelColor_t dimmedBlinkColor;


  // Handle the exceeding of the allowed range:
  if (climaValue > maxValue) {
    climaValue = maxValue;
    inUpperLimit = 1;
  }
  if (climaValue < minValue) {
    climaValue = minValue;
    inLowerLimit = 1;
  }

  // Background for ReferenceArea
  dimmedRefColor.r = refRange.r * dimmPercent / 100;
  dimmedRefColor.g = refRange.g * dimmPercent / 100;
  dimmedRefColor.b = refRange.b * dimmPercent / 100;


  for (int i = map(refLSL, minValue, maxValue, minLED, maxLED);
       i <= map(refUSL, minValue, maxValue, minLED, maxLED); i++)  {
    stripe.pixels[i] = dimmedRefColor;
  }

  // LightDot Position
  int activeLED = map(climaValue, minValue, maxValue, minLED, maxLED);

  // make a smooth blinking
  blinkBrightness = 0.92 * blinkBrightness + 0.08 * blinkingOn * dimmPercent;

  if (inUpperLimit || inLowerLimit) {
    dimmedBlinkColor.r = activeColor.r * blinkBrightness / 100;
    dimmedBlinkColor.g = activeColor.g * blinkBrightness / 100;
    dimmedBlinkColor.b = activeColor.b * blinkBrightness / 100;
    stripe.pixels[activeLED] = dimmedBlinkColor;
  }
  else
  {
    stripe.pixels[activeLED] = dimmedColor;
  }

}

void updateHistory() {
  //Serial.println("---------------");

  // Track the amount of availabel history data (only relevant during "ramp up")
  if (historyPointsCount < historyPoints) {
    historyPointsCount ++;
  }

  // Shift all array elements one position forward and put each element 'on screen'
  for (int i = historyPointsCount - 2; i >= 0; i--) {
    climaHistory[i + 1] = climaHistory[i];
    updateLEDs(climaHistory[i + 1]);
    delay (50);
  }
  // Add current data on 1st position
  climaHistory[0] = clima;
  if (historyPointsCount < 27) {
    Serial.print (historyPointsCount);
    Serial.println(" History points stored.");
    //Serial.println("---------------");
  }

  // Calculate trend for pressure
  if (historyPointsCount > 26) {  //Only if >50% of the normal history are collected
    int avrgOldPressure = climaHistory[24].p / 3 +
                          climaHistory[25].p / 3 +
                          climaHistory[26].p / 3;
    int avrgNewPressure = climaHistory[0].p / 3 +
                          climaHistory[1].p / 3 +
                          climaHistory[2].p / 3;
    climaHistory[0].pTrendTwoHours =   avrgNewPressure - avrgOldPressure;
    Serial.print("p Trend:");
    Serial.println(climaHistory[0].pTrendTwoHours);
  }
}

void handleGesture() {
  Serial.printf("Gesture triggered %u times\n", gestureAPDS.eventsCounter);
  //gestureAPDS.active = false;
  if ( apds.isGestureAvailable() ) {
    switch ( apds.readGesture() ) {
      case DIR_UP:
        Serial.println("UP");
        break;
      case DIR_DOWN:
        Serial.println("DOWN");
        break;
      case DIR_LEFT:
        Serial.println("LEFT");
        viewNumber = max(0, viewNumber - 1);
        Serial.printf("Current view is %u .\n", viewNumber);
        break;
      case DIR_RIGHT:
        Serial.println("RIGHT");
        viewNumber = min(4, viewNumber + 1);
        Serial.printf("Current view is %u .\n", viewNumber);
        break;
      case DIR_NEAR:
        Serial.println("NEAR");
        break;
      case DIR_FAR:
        Serial.println("FAR");
        break;
      default:
        Serial.println("NONE");
    }
  } else {
    Serial.println(".");
  }
  Serial.println("GestureHandled");
}
