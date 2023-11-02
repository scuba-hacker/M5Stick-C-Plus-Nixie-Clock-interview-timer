// Mark B Jones 2023 - will update with full info regarding the original author who wrote the M5StickC version with just the clock.
// I have added a count-up and count-down timer to their code.
//
// Original Author's date of writing:
// M5StickC Nixie tube Clock: 2019.06.06 

#include <M5StickCPlus.h>

#include "secrets.c"

#include <WiFi.h>
#include "time.h" 

#include <AsyncTCP.h>           // OTA updates
#include <ESPAsyncWebServer.h>  // OTA updates
#include <AsyncElegantOTA.h>    // OTA updates

#include "vfd_18x34.c"
#include "vfd_35x67.c"

AsyncWebServer server(80);      // OTA updates
const bool enableOTAServer=true; // OTA updates

Button* p_primaryButton = NULL;
Button* p_secondButton = NULL;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;        // timezone offset
int   daylightOffset_sec = 0;

RTC_TimeTypeDef RTC_TimeStruct;
RTC_DateTypeDef RTC_DateStruct;

int mode_ = 3; // 3:2Lines 2: 2Lines(YYMM), 1:1Line
const uint8_t*n[] = { // vfd font 18x34
  vfd_18x34_0,vfd_18x34_1,vfd_18x34_2,vfd_18x34_3,vfd_18x34_4,
  vfd_18x34_5,vfd_18x34_6,vfd_18x34_7,vfd_18x34_8,vfd_18x34_9
  };
const uint8_t*m[] = { // vfd font 35x67
  vfd_35x67_0,vfd_35x67_1,vfd_35x67_2,vfd_35x67_3,vfd_35x67_4,
  vfd_35x67_5,vfd_35x67_6,vfd_35x67_7,vfd_35x67_8,vfd_35x67_9
  };
const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const int defaultBrightness = 100;

int countdownFrom=59;
bool haltCountdown=false;
bool showDate=true;

bool showPowerStats=false;

void resetClock();

void resetCountDownTimer();
void resetCountUpTimer();

const bool enableShutdownOnNoUSB = false;

const float minimumUSBVoltage=2.0;
long USBVoltageDropTime=0;
long milliSecondsToWaitForShutDown=3000;

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout);

void updateButtonsAndBuzzer();

void shutdownIfUSBPowerOff()
{
  if (M5.Axp.GetVBusVoltage() < minimumUSBVoltage)
  {
    if (USBVoltageDropTime == 0)
      USBVoltageDropTime=millis();
    else 
    {
      if (millis() > USBVoltageDropTime + milliSecondsToWaitForShutDown)
      {
        // initiate shutdown after 3 seconds.
       delay(1000);
       fadeToBlackAndShutdown();
      }
    }
  }
  else
  {
    if (USBVoltageDropTime != 0)
      USBVoltageDropTime = 0;
  }
}

void  initialiseRTCfromNTP()
{
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(20,50);

  int maxAttempts=50;

  if (!enableOTAServer)
  {
    //connect to WiFi
    Serial.printf("Connect to %s ", ssid_1);
    M5.Lcd.printf("Connect to\n    %s\n", ssid_1);
    WiFi.begin(ssid_1, password_1);
    while (WiFi.status() != WL_CONNECTED && --maxAttempts) {
        delay(300);
        Serial.print(".");
        M5.Lcd.print(".");
    }
  }
    
  if (maxAttempts)
  {
    Serial.println(" OK");
    M5.Lcd.print(" OK");
  
    //init and get the time
    _initialiseTimeFromNTP:
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("No time available (yet)");
      // Let RTC continue with existing settings
      delay(1000);
    }
    else
    {
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
      // Use NTP to update RTC
    
      RTC_TimeTypeDef TimeStruct;
      TimeStruct.Hours   = timeinfo.tm_hour;
      TimeStruct.Minutes = timeinfo.tm_min;
      TimeStruct.Seconds = timeinfo.tm_sec;
      M5.Rtc.SetTime(&TimeStruct);

      RTC_DateTypeDef DateStruct;
      DateStruct.Month = timeinfo.tm_mon+1;
      DateStruct.Date = timeinfo.tm_mday;
      DateStruct.Year = timeinfo.tm_year+1900;
      DateStruct.WeekDay = timeinfo.tm_wday;
      M5.Rtc.SetDate(&DateStruct);    
      if (daylightOffset_sec == 0)
        M5.Lcd.println("\n RTC to GMT");
      else
        M5.Lcd.println("\n RTC to BST");
  
      delay(300);
    }

    if (daylightOffset_sec == 0)
    {
      // check if British Summer Time

      int day = timeinfo.tm_wday;
      int date = timeinfo.tm_mday;
      int month = timeinfo.tm_mon;

      if (month == 2 && date > 24)    // is date after or equal to last Sunday in March?
      {
        if (date - day >= 25)
        {
          daylightOffset_sec=3600;
          // reinitialise time from NTP with correct offset.
          // this doesn't deal with the exact changeover time for BST, but doesn't matter
          goto _initialiseTimeFromNTP;
        }
          /*
        day == 0 && date >= 25   TRUE
        day == 1 && date >= 26   TRUE
        day == 2 && date >= 27   TRUE
        day == 3 && date >= 28   TRUE
        day == 4 && date >= 29   TRUE
        day == 5 && date >= 30   TRUE
        day == 6 && date >= 31   TRUE
        */
      }
    }

    if (!enableOTAServer)
    {
        //disconnect WiFi as it's no longer needed
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
  }
  else
  {
    M5.Lcd.println(" FAILED");
  }

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setRotation(1);

  resetClock();
}

void setup()
{ 
  M5.begin();

  delay(2000);

  p_primaryButton = &M5.BtnA;
  p_secondButton = &M5.BtnB;

  M5.Lcd.setRotation(1);
  M5.Lcd.setTextSize(2);
  M5.Axp.ScreenBreath(defaultBrightness);
  
  Serial.begin(115200);

  if (enableOTAServer)
  {
    if (!setupOTAWebServer(ssid_1, password_1, label_1, timeout_1))
      setupOTAWebServer(ssid_2, password_2, label_2, timeout_2);
  }

  initialiseRTCfromNTP();
}

void resetCountUpTimer()
{
  M5.Lcd.fillScreen(BLACK);
  RTC_TimeTypeDef TimeStruct;         // Hours, Minutes, Seconds 
  TimeStruct.Hours   = 0;
  TimeStruct.Minutes = 0;
  TimeStruct.Seconds = 0;

  M5.Rtc.SetTime(&TimeStruct);
  mode_ = 1;
}

void resetCountDownTimer()
{
  haltCountdown=false;
  resetCountUpTimer();
  mode_ = 4;
}

void resetClock()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  
  M5.Lcd.fillScreen(BLACK);
  RTC_TimeTypeDef TimeStruct;
  TimeStruct.Hours   = timeinfo.tm_hour;
  TimeStruct.Minutes = timeinfo.tm_min;
  TimeStruct.Seconds = timeinfo.tm_sec;
  M5.Rtc.SetTime(&TimeStruct);

  RTC_DateTypeDef DateStruct;
  DateStruct.Month = timeinfo.tm_mon+1;
  DateStruct.Date = timeinfo.tm_mday;
  DateStruct.Year = timeinfo.tm_year+1900;
  DateStruct.WeekDay = timeinfo.tm_wday;
  M5.Rtc.SetDate(&DateStruct);    

  mode_ = 3; // change back to 3
}

void fadeToBlackAndShutdown()
{
  for (int i = 90; i > 0; i=i-15)
  {
    M5.Axp.ScreenBreath(i);             // fade to black
    delay(100);
  }

  M5.Axp.PowerOff();
}

bool checkButtons()
{
  bool changeMade = false;
  
  M5.update();
  
  if (M5.BtnA.wasReleasefor(3000))
  {
    fadeToBlackAndShutdown();
  }
  else
  {
    if (M5.BtnA.wasReleasefor(100))
    {
      // Switch display modes
      if (mode_ == 4) // countdown mode, next is clock
      {
        resetClock(); changeMade = true;
      }
      else if (mode_ == 3) // clock mode, next is timer
      {
        resetCountUpTimer(); changeMade = true;
      }
      else if (mode_ == 1) // countup timer mode, next is countdown
      {
        resetCountDownTimer(); changeMade = true;
      }
     }
  }

  if (M5.BtnB.wasReleasefor(1000))
  {
    if (mode_ == 4)
    {
      // revert the countdown timer to the start
      resetCountDownTimer(); changeMade = true;
    }
    else if (mode_ == 1)
    {
      // revert the countup timer to the start
      resetCountUpTimer();
    }
    else if (mode_ == 3) // clock mode
    {
      // reinitialise Time from UTP
    initialiseRTCfromNTP();
    }
  }
  else if (M5.BtnB.wasReleasefor(100))
  {
    if (mode_ == 4)       // Countdown mode, reduce timer by 15 mins
    {
      countdownFrom=countdownFrom-15;
      if (countdownFrom <= 0)
        countdownFrom = 59;
      changeMade = true;
    }
    else if (mode_ == 3)  // Clock mode
    {
      showDate=!showDate; changeMade = true;
      M5.Lcd.fillScreen(BLACK);
    }
    else if (mode_ == 1)  // Count Up Timer mode - reset count-up timer to 0
    {
      resetCountUpTimer(); changeMade = true;  // zero the count-up timer
    }
  }
  return changeMade;
}

void loop(void)
{ 
  if (enableShutdownOnNoUSB)
    shutdownIfUSBPowerOff();

  if ( mode_ == 4) { vfd_4_line_countdown(countdownFrom);}   // mm,ss, optional dd mm
  if ( mode_ == 3 ){ vfd_3_line_clock();}   // hh,mm,ss, optional dd mm
  if ( mode_ == 2 ){ vfd_2_line();}   // yyyy,mm,dd,hh,mm,ss - not used.
  if ( mode_ == 1 ){ vfd_1_line_countup();}   // mm,ss, optional dd mm

  for (int m=0;m<10;m++)
  {
    delay(50);
    if (checkButtons()) // If a change occurred break out of wait loop to make change asap.
      break;
  }
}

void vfd_4_line_countdown(const int countdownFrom){ // Countdown mode, minutes, seconds
  int minutesRemaining = 0, secondsRemaining = 0;
  
  if (!haltCountdown)
  {
    M5.Rtc.GetTime(&RTC_TimeStruct);
    M5.Rtc.GetDate(&RTC_DateStruct);
    int minutesRemaining = countdownFrom - RTC_TimeStruct.Minutes;
    int secondsRemaining = 59 - RTC_TimeStruct.Seconds;
        
    int i1 = int(minutesRemaining / 10 );
    int i2 = int(minutesRemaining - i1*10 );
    int s1 = int(secondsRemaining / 10 );
    int s2 = int(secondsRemaining - s1*10 );
    
    M5.Lcd.pushImage(  2,6,35,67, (uint16_t *)m[i1]);
    M5.Lcd.pushImage( 41,6,35,67, (uint16_t *)m[i2]);
    M5.Lcd.drawPixel( 79,28, ORANGE); M5.Lcd.drawPixel( 79,54,ORANGE); 
    M5.Lcd.drawPixel( 79,27, YELLOW); M5.Lcd.drawPixel( 79,53,YELLOW); 
    M5.Lcd.pushImage( 83,6,35,67, (uint16_t *)m[s1]);
    M5.Lcd.pushImage(121,6,35,67, (uint16_t *)m[s2]);

    drawDate();

    if ( s1 == 0 && s2 == 0 ){ fade();}

    if (minutesRemaining == 0 && secondsRemaining == 0)
    {
      haltCountdown=true;
    }
  }
  else
  {
    fade();
    fade();
    fade();
    fade();
    fade();
  }  
}
 
void vfd_3_line_clock(){    // Clock mode - Hours, mins, secs with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int h1 = int(RTC_TimeStruct.Hours / 10 );
  int h2 = int(RTC_TimeStruct.Hours - h1*10 );
  int i1 = int(RTC_TimeStruct.Minutes / 10 );
  int i2 = int(RTC_TimeStruct.Minutes - i1*10 );
  int s1 = int(RTC_TimeStruct.Seconds / 10 );
  int s2 = int(RTC_TimeStruct.Seconds - s1*10 );
  
  M5.Lcd.pushImage(  2,0,35,67, (uint16_t *)m[h1]);
  M5.Lcd.pushImage( 41,0,35,67, (uint16_t *)m[h2]);
  M5.Lcd.drawPixel( 79,22, ORANGE); M5.Lcd.drawPixel( 79,48,ORANGE); 
  M5.Lcd.drawPixel( 79,21, YELLOW); M5.Lcd.drawPixel( 79,47,YELLOW); 
  M5.Lcd.pushImage( 83,0,35,67, (uint16_t *)m[i1]);
  M5.Lcd.pushImage(121,0,35,67, (uint16_t *)m[i2]);
  M5.Lcd.pushImage(120,45,18,34, (uint16_t *)n[s1]);
  M5.Lcd.pushImage(140,45,18,34, (uint16_t *)n[s2]);

  drawDate();

  // print current and voltage of USB
  if (showPowerStats)
  {
    M5.Lcd.setCursor(5,5);
    M5.Lcd.printf("USB %.1fV, %.0fma\n",  M5.Axp.GetVBusVoltage(),M5.Axp.GetVBusCurrent());
    M5.Lcd.printf("Batt Charge %.0fma\n",  M5.Axp.GetBatChargeCurrent());
    M5.Lcd.printf("Batt %.1fV %.0fma\n",  M5.Axp.GetBatVoltage(), M5.Axp.GetBatCurrent());
  }
   
  if ( s1 == 0 && s2 == 0 ){ fade();}
}
 
void vfd_1_line_countup(){  // Timer Mode - Minutes and Seconds, with optional date
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  int i1 = int(RTC_TimeStruct.Minutes / 10 );
  int i2 = int(RTC_TimeStruct.Minutes - i1*10 );
  int s1 = int(RTC_TimeStruct.Seconds / 10 );
  int s2 = int(RTC_TimeStruct.Seconds - s1*10 );
  
  M5.Lcd.pushImage(  2,6,35,67, (uint16_t *)m[i1]);
  M5.Lcd.pushImage( 41,6,35,67, (uint16_t *)m[i2]);
  M5.Lcd.drawPixel( 79,28, ORANGE); M5.Lcd.drawPixel( 79,54,ORANGE); 
  M5.Lcd.drawPixel( 79,27, YELLOW); M5.Lcd.drawPixel( 79,53,YELLOW); 
  M5.Lcd.pushImage( 83,6,35,67, (uint16_t *)m[s1]);
  M5.Lcd.pushImage(121,6,35,67, (uint16_t *)m[s2]);

  drawDate();

  if ( s1 == 0 && s2 == 0 ){ fade();}
}

void drawDate()
{
  if (showDate)
  {
    int j1 = int(RTC_DateStruct.Month   / 10);
    int j2 = int(RTC_DateStruct.Month   - j1*10 );
    int d1 = int(RTC_DateStruct.Date    / 10 );
    int d2 = int(RTC_DateStruct.Date    - d1*10 );
  
    M5.Lcd.pushImage(35, 75,18,34, (uint16_t *)n[d1]);
    M5.Lcd.pushImage(54, 75,18,34, (uint16_t *)n[d2]);
    M5.Lcd.pushImage(85, 75 ,18,34, (uint16_t *)n[j1]);
    M5.Lcd.pushImage(105, 75,18,34, (uint16_t *)n[j2]);
  }
}

void fade(){
  for (int i=0;i<100;i=i+15){M5.Axp.ScreenBreath(i);delay(25);}
  for (int i=100;i>0;i=i-15){M5.Axp.ScreenBreath(i);delay(25);}
  M5.Axp.ScreenBreath(defaultBrightness);
}

void vfd_2_line(){      // Unused mode - full date and time with year.
  M5.Rtc.GetTime(&RTC_TimeStruct);
  M5.Rtc.GetDate(&RTC_DateStruct);
  //Serial.printf("Data: %04d-%02d-%02d\n",RTC_DateStruct.Year,RTC_DateStruct.Month,RTC_DateStruct.Date);
  //Serial.printf("Week: %d\n",RTC_DateStruct.WeekDay);
  //Serial.printf("Time: %02d : %02d : %02d\n",RTC_TimeStruct.Hours,RTC_TimeStruct.Minutes,RTC_TimeStruct.Seconds);
  // Data: 2019-06-06
  // Week: 0
  // Time: 09 : 55 : 26
  int y1 = int(RTC_DateStruct.Year    / 1000 );
  int y2 = int((RTC_DateStruct.Year   - y1*1000 ) / 100 );
  int y3 = int((RTC_DateStruct.Year   - y1*1000 - y2*100 ) / 10 );
  int y4 = int(RTC_DateStruct.Year    - y1*1000 - y2*100 - y3*10 );
  int j1 = int(RTC_DateStruct.Month   / 10);
  int j2 = int(RTC_DateStruct.Month   - j1*10 );
  int d1 = int(RTC_DateStruct.Date    / 10 );
  int d2 = int(RTC_DateStruct.Date    - d1*10 );
  int h1 = int(RTC_TimeStruct.Hours   / 10) ;
  int h2 = int(RTC_TimeStruct.Hours   - h1*10 );
  int i1 = int(RTC_TimeStruct.Minutes / 10 );
  int i2 = int(RTC_TimeStruct.Minutes - i1*10 );
  int s1 = int(RTC_TimeStruct.Seconds / 10 );
  int s2 = int(RTC_TimeStruct.Seconds - s1*10 );
   
  M5.Lcd.pushImage(  0, 0,18,34, (uint16_t *)n[y1]); 
  M5.Lcd.pushImage( 19, 0,18,34, (uint16_t *)n[y2]);
  M5.Lcd.pushImage( 38, 0,18,34, (uint16_t *)n[y3]);
  M5.Lcd.pushImage( 57, 0,18,34, (uint16_t *)n[y4]);
  M5.Lcd.drawPixel( 77,13, ORANGE); M5.Lcd.drawPixel( 77,23,ORANGE);
  M5.Lcd.pushImage( 80, 0,18,34, (uint16_t *)n[j1]);
  M5.Lcd.pushImage( 99, 0,18,34, (uint16_t *)n[j2]);
  M5.Lcd.drawPixel(118,13, ORANGE); M5.Lcd.drawPixel(119,23,ORANGE);
  M5.Lcd.pushImage(120, 0,18,34, (uint16_t *)n[d1]);
  M5.Lcd.pushImage(140, 0,18,34, (uint16_t *)n[d2]);
                                                    
  M5.Lcd.pushImage( 00,40,18,34, (uint16_t *)n[h1]);
  M5.Lcd.pushImage( 20,40,18,34, (uint16_t *)n[h2]);
  M5.Lcd.drawPixel( 48,54, ORANGE); M5.Lcd.drawPixel( 48,64,ORANGE); 
  M5.Lcd.pushImage( 60,40,18,34, (uint16_t *)n[i1]);
  M5.Lcd.pushImage( 80,40,18,34, (uint16_t *)n[i2]);
  M5.Lcd.drawPixel(108,54, ORANGE); M5.Lcd.drawPixel(108,64,ORANGE);
  M5.Lcd.pushImage(120,40,18,34, (uint16_t *)n[s1]);
  M5.Lcd.pushImage(140,40,18,34, (uint16_t *)n[s2]);
 
  if ( i1 == 0 && i2 == 0 ){ fade();}
}

bool setupOTAWebServer(const char* _ssid, const char* _password, const char* label, uint32_t timeout)
{
  bool forcedCancellation = false;
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  bool connected = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(_ssid, _password);

  // Wait for connection for max of timeout/1000 seconds
  M5.Lcd.printf("%s Wifi", label);
  int count = timeout / 500;
  while (WiFi.status() != WL_CONNECTED && --count > 0)
  {
    // check for cancellation button - top button.
    updateButtonsAndBuzzer();

    if (p_primaryButton->isPressed()) // cancel connection attempts
    {
      forcedCancellation = true;
      break;
    }

    M5.Lcd.print(".");
    delay(500);
  }
  M5.Lcd.print("\n\n");

  if (WiFi.status() == WL_CONNECTED)
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(200, "text/plain", "To upload firmware use /update");
    });

    AsyncElegantOTA.begin(&server);    // Start AsyncElegantOTA
    server.begin();

    M5.Lcd.print(WiFi.localIP());
    M5.Lcd.print(" ");
    M5.Lcd.print(WiFi.macAddress());
    M5.Lcd.print("\n");

    M5.Lcd.print("Connected");
    connected = true;

    updateButtonsAndBuzzer();

    if (p_secondButton->pressedFor(15))
    {
      M5.Lcd.print("\n\n20\nsecond pause");
      delay(20000);
    }
  }
  else
  {
    if (forcedCancellation)
      M5.Lcd.print("\n     Cancelled\n Connection Attempts");
    else
      M5.Lcd.print("No Connection");
  }

  delay(1000);

  M5.Lcd.fillScreen(TFT_BLACK);

  return connected;
}

void updateButtonsAndBuzzer()
{
  p_primaryButton->read();
  p_secondButton->read();
  M5.Beep.update();
}
