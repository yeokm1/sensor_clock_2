#include <SPI.h>
#include <Wire.h>
#include <JeeLib.h>

#include <DHT.h>
#include <RTC_DS1307.h>

#include <LCD03.h>

#define BATT_PIN A3

#define LED_13_PIN 13

#define LDR_PIN A1

#define DHTPIN A0
#define DHTTYPE DHT22

#define LCD_CHAR_LENGTH 16

#define LCD_DEGREES_SYMBOL (char) 223

#define REFRESH_INTERVAL 5 //Minimum 5 seconds for DHT22 to refresh. Do not exceed 59


#define SLEEP_INTERVAL 250 //0.25s
#define SLEEP_LONG_INTERVAL 45000 //45s

//Slight difference in values to prevent hysteresis effect on the output, constantly turning on and off the LCD
#define LIGHT_THRESHOLD_OFF 55 //Turn off when higher than this
#define LIGHT_THRESHOLD_ON 45 //Turn on when lower than this


#define BATT_MILLIVOLT_NOT_TURN_ON_LIGHT_THRESHOLD 3500

#define OFF_HOUR 00
#define OFF_MIN 00

#define ON_HOUR 07 //Must be bigger than OFF_HOUR
#define ON_MIN 00

#define TURN_OFF_AT_TIMES true  //Set to false if you don't want the screen and readings to stop at certain times.

#define THERM_ICON_REP 1
#define HUM_ICON_REP 2

//Icons from http://www.instructables.com/id/Clock-with-termometer-using-Arduino-i2c-16x2-lcd-D/
byte thermIcon[8] = //icon for thermometer
{
  B00100,
  B01010,
  B01010,
  B01110,
  B01110,
  B11111,
  B11111,
  B01110
};

byte humIcon[8] = //icon for humidity
{
  B00100,
  B00100,
  B01010,
  B01010,
  B10001,
  B10001,
  B10001,
  B01110,
};


DHT dht(DHTPIN, DHTTYPE);
RTC_DS1307 RTC;  //Code for this works although I use DS3231. For the initial time setting convenience.
ISR(WDT_vect) {
  Sleepy::watchdogEvent();  // Setup for low power waiting
}
// Create new LCD03 instance
LCD03 lcd;

int prevIntervalReadingSecond = 0;
int prevUpdateTimeSecond = 0;
int prevUpdateTurnOnAndOffMinute = 0;

bool currentlyOn = true;

bool isLcdBacklightOn = false;

void setup() {
  Serial.begin(9600);

  //Disable LED Pin 13 (Optional). I just hate the constant LED light.
  pinMode(LED_13_PIN, OUTPUT);
  digitalWrite(LED_13_PIN, LOW);

  //Start RTC
  Wire.begin();
  RTC.begin();

  if (!RTC.isrunning()) {
    Serial.println("RTC is NOT running");
  }

  // This section grabs the current datetime and compares it to
  // the compilation time.  If necessary, the RTC is updated.
  DateTime now = RTC.now();
  DateTime compiled = DateTime(__DATE__, __TIME__);

  if (now.unixtime() < compiled.unixtime()) {
    Serial.println("RTC is older than compile time! Updating");
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }

  //Start LCD
  lcd.begin(LCD_CHAR_LENGTH, 2);


  lcd.createChar(THERM_ICON_REP, thermIcon);
  lcd.createChar(HUM_ICON_REP, humIcon);

  changeBacklightStatus(false);

  printThisOnLCDLine("By: YKM", 1);


  //Start DTT
  dht.begin();


}

void loop() {
  DateTime now = RTC.now();


  int second = now.second();
  int minute = now.minute();
  int hour = now.hour();

  int lightValue = analogRead(LDR_PIN);
  Serial.println("LDR");
  Serial.println(lightValue);

  if (currentlyOn) {

    //Update time only if second changes. Prevent needless requests to LCD
    if (second != prevUpdateTimeSecond) {
      prevUpdateTimeSecond = second;
      String dateString = generateDateTimeString(now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
      printThisOnLCDLine(dateString, 0);



      if (lightValue < LIGHT_THRESHOLD_ON && !isLcdBacklightOn) {
        int battMilliVolt = getBatteryMilliVoltage();

        if (battMilliVolt > BATT_MILLIVOLT_NOT_TURN_ON_LIGHT_THRESHOLD) {
          changeBacklightStatus(true);
          isLcdBacklightOn = true;
        }

      }

      if (lightValue > LIGHT_THRESHOLD_OFF && isLcdBacklightOn) {
        changeBacklightStatus(false);
        isLcdBacklightOn = false;
      }
    }

    //Every refresh interval
    if ((second % REFRESH_INTERVAL) == 0 && second != prevIntervalReadingSecond) {
      //Prevent multiple readings every interval
      prevIntervalReadingSecond = second;
      Serial.println();
      Serial.print("Take DHT22 Readings: ");


      float DHTTemp = dht.readTemperature();
      float DHTHum = dht.readHumidity();


      Serial.print(DHTHum);
      Serial.print("\t");
      Serial.println(DHTTemp);





      generateAndPrintTempHumString(DHTTemp, DHTHum);




    }
  }




  //Prevent too many execution since only need to check once a minute
  if (TURN_OFF_AT_TIMES && prevUpdateTurnOnAndOffMinute != minute) {
    prevUpdateTurnOnAndOffMinute = minute;

    Serial.println("Checking time");
    if (currentlyOn) {

      if (((hour == OFF_HOUR && minute >= OFF_MIN)
           || (hour > OFF_HOUR && hour < ON_HOUR)) //Exceeded OFF time. The moment display turns on to indicate lights out, go to sleep.
          && isLcdBacklightOn) { //Don't enter sleep mode if background is still bright enough

        currentlyOn = false;
        lcd.clear();
        changeBacklightStatus(false);
        isLcdBacklightOn = false;
        longSleep();
      }

    } else {

      if ((lightValue > LIGHT_THRESHOLD_OFF) //If background bright enough during off hour, turns thing back on
          || (hour == ON_HOUR && minute == ON_MIN)) {
        currentlyOn = true;
      } else {
        longSleep();
      }

    }
  }


  shortSleep();
  //delay(250);

}

void changeBacklightStatus(bool on) {
  if (on) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
  }


}

void shortSleep() {
  Sleepy::loseSomeTime(SLEEP_INTERVAL);

}


void longSleep() {
  Sleepy::loseSomeTime(SLEEP_LONG_INTERVAL);
}

int getBatteryMilliVoltage() {
  int battValue = analogRead(BATT_PIN);
  int battMilliVoltage = (((float) battValue) / 1024) * 5000;
  Serial.print("Batt: ");
  Serial.println(battMilliVoltage);

  return battMilliVoltage;

}


void generateAndPrintTempHumString(float temp, float hum) {

  String tempString = ftoa(temp, 1);
  String humString = ftoa(hum, 1);

  String intermediate1 = " " + tempString + LCD_DEGREES_SYMBOL + "C ";
  String intermediate2 = " " + humString + "%";

  lcd.setCursor (0, 1);
  lcd.write((byte) THERM_ICON_REP);
  lcd.print(intermediate1);
  lcd.write((byte) HUM_ICON_REP);
  lcd.print(intermediate2);

}


String generateTempHumString(float temp, float hum) {
  String tempString = ftoa(temp, 1);
  String humString = ftoa(hum, 1);
  String result = "P:" + tempString + LCD_DEGREES_SYMBOL + "C H:" + humString + "%";
  return result;

}


//Convert float/double to String as ardiuno sprintf does not support float/double
String ftoa(double f, int precision)
{
  long p[] = {0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};

  char temp[5];
  char * a = temp;
  long heiltal = (long)f;
  itoa(heiltal, a, 10);
  while (*a != '\0') a++;
  *a++ = '.';
  long desimal = abs((long)((f - heiltal) * p[precision]));
  itoa(desimal, a, 10);
  String result = temp;
  return result;
}

void printThisOnLCDLine(String text, int line) {

  //Clear current line only if the new line cannot occupy the old line
  if (text.length() < LCD_CHAR_LENGTH) {
    lcd.setCursor (0, line);
    lcd.print("                ");
  }

  lcd.setCursor (0, line);
  lcd.print(text);
}

String generateDateTimeString(int year, int month, int day, int hour, int minute, int second) {
  char buff[3];

  sprintf(buff, "%02d", hour);
  String hourString = buff;

  sprintf(buff, "%02d", minute);
  String minuteString = buff;

  sprintf(buff, "%02d", second);
  String secondString = buff;

  String result = hourString + ":" + minuteString + ":" + secondString + " ";

  String dayOfWeek = getDayOfTheWeek(year, month, day);

  sprintf(buff, "%02d", day);
  String dayString = buff;

  String monthString = getMonth(month);

  if (0 <= second && second <= 40) {
    result += " " + dayOfWeek + " " + dayString;
  } else {
    result += monthString + year;
  }
  return result;
}



// From http://stackoverflow.com/a/21235587
String getDayOfTheWeek(int y, int m, int d) {
  String weekdayname[] = {"Sun", "Mon", "Tue",
                          "Wed", "Thu", "Fri", "Sat"
                         };

  int weekday = (d += m < 3 ? y-- : y - 2, 23 * m / 9 + d + 4 + y / 4 - y / 100 + y / 400) % 7;


  return weekdayname[weekday];
}

String getMonth(int month) {
  switch (month) {
    case 1: return "Jan";
    case 2: return "Feb";
    case 3: return "Mar";

    case 4: return "Apr";
    case 5: return "May";
    case 6: return "Jun";

    case 7: return "Jul";
    case 8: return "Aug";
    case 9: return "Sep";

    case 10: return "Oct";
    case 11: return "Nov";
    case 12: return "Dec";
    default : return "Nil";
  }

}





