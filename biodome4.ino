#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <DS3232RTC.h> //source: https://github.com/JChristensen/DS3232RTC 
#include <SD.h>
#include <DallasTemperature.h> // includes OneWire.h

// watchdog intervals
// sleep bit patterns for WDTCSR
// defined in the datasheet, using bits 0-2 and 5 of WDTCSR
enum 
{
  WDT_16_MS  =  0b000000,
  WDT_32_MS  =  0b000001,
  WDT_64_MS  =  0b000010,
  WDT_128_MS =  0b000011,
  WDT_256_MS =  0b000100,
  WDT_512_MS =  0b000101,
  WDT_1_SEC  =  0b000110,
  WDT_2_SEC  =  0b000111,
  WDT_4_SEC  =  0b100000,
  WDT_8_SEC  =  0b100001,
 };  // end of WDT intervals enum

const byte POWER = 8;
time_t tm;
const int chipSelect = 10;

const int TMPDATA = 7; // One wire bus
byte tc26[8] = {0x3B, 0x52, 0x78, 0x18, 0x00, 0x00, 0x00, 0x95}; //#26
OneWire oneWire(TMPDATA);
DallasTemperature sensors(&oneWire);

//SD CARD
File logfile;
char fname[30] ;


// watchdog interrupt
ISR (WDT_vect) 
{
    wdt_disable();  // disable watchdog
}

// sleep for "interval" mS as in enum above
void myWatchdogEnable (const byte interval) 
{
    // timed sequence coming up, so cancel interrupts:
    noInterrupts ();

    // clear various "reset" flags
    MCUSR = 0;

    // The following two lines are timed operations that must be called in
    // order and within 4 clock cycles of each other to effect changes to
    // WDTCSR:
    
    // allow changes, disable reset
    WDTCSR = bit (WDCE) | bit (WDE);
    // set interrupt mode and an interval 
    WDTCSR = bit (WDIE) | interval;    // set WDIE, and requested delay
    wdt_reset();  // macro to reset the WDT: calls `__asm__ __volatile__ ("wdr")`

    // At this point the WDT is set to fire an interrupt every interval

    // Next, we disable the ADC, then reduce power to peripherals. We need
    // to explicitly disable ADC, as power_all_disable() would otherwise
    // leave it locked 'on'. Not sure what this means, see page 65 of the
    // Atmel ATmega328/P datasheet.
    
    // disable ADC, storing current value
    byte old_ADCSRA = ADCSRA;
    ADCSRA = 0;  
    ADMUX = 0;  // turn off internal Vref
  
    // turn off various modules
    power_all_disable ();
  
    // Digital Input Disable Register on analogue pins
    DIDR0 = bit (ADC0D) | bit (ADC1D) | bit (ADC2D) | bit (ADC3D) | bit (ADC4D) | bit (ADC5D);
    DIDR1 = bit (AIN1D) | bit (AIN0D);

    //buttonPressed = false;
    //attachInterrupt (0, wake, LOW);
  
    // ready to sleep
    set_sleep_mode (SLEEP_MODE_PWR_DOWN);  
    sleep_enable();

    // turn off brown-out enable in software
    // BODS must be set to one and BODSE must be set to zero within four clock cycles
    MCUCR = bit (BODS) | bit (BODSE);
    // The BODS bit is automatically cleared after three clock cycles
    MCUCR = bit (BODS); 
    interrupts ();  // one cycle
    sleep_cpu ();   // one cycle

    // cancel sleep as a precaution
    sleep_disable();
    // power-on internal modules 
    power_all_enable ();
    // put ADC back to previous value (i.e., turn it on if it was on,
    // turn it off if it was off prior to sleeping)
    ADCSRA = old_ADCSRA;
  
} // end of myWatchdogEnable

/* Needed only for responding to button presses:
void wake ()
{
    // cancel sleep as a precaution
    sleep_disable();
    // must do this as the pin will probably stay low for a while
    detachInterrupt (0);
    // note button was pressed
    // buttonPressed = true;
}  // end of wake
*/


void setup ()
{
    pinMode(POWER, OUTPUT);
    digitalWrite(POWER, HIGH);
    pinMode(chipSelect, OUTPUT);
    // wait for RTC to be ready:
    delay(100);
    tm = RTC.get();
    Serial.begin (115200);
    Serial.print("Logging started: ");

    char buf[30];
    sprintf (buf, "%04d-%02d-%02d %02d:%02d:%02d", 
             (int) year(tm), (int) month(tm), (int) day(tm),  
             (int) hour(tm), (int) minute(tm), (int) second(tm));

    Serial.println(buf);

    sprintf(fname, "%02d%02d%02d%02d.csv",
            (int) month(tm), (int) day(tm),  
            (int) hour(tm), (int) minute(tm));
            
    Serial.print("Filename: ");
    Serial.println(fname);

    Serial.print("Initializing SD card...");
    if (!SD.begin(chipSelect)) {
        Serial.println("Card failed, or not present");
        // don't do anything more:
        return;
    }
    
    Serial.println("card initialized.");

    logfile = SD.open(fname, FILE_WRITE);
    if (!logfile) {
        error("could not create file");
    }
    logfile.flush();
    logfile.close();
    
    Serial.flush ();
    Serial.end ();
    pinMode(chipSelect, INPUT);
    digitalWrite(POWER, LOW);
}

int counter;

void loop () 
{
    // sleep for 8 seconds
    myWatchdogEnable (WDT_1_SEC);

    // count number of times we slept
    counter++;
    Serial.begin (115200);
    Serial.print (".");
    
    // every 296 seconds record data (37 lots of 8 seconds asleep)
    
    // this is slightly less than 5 minutes, and accuracy isn't likely to
    // be too great. However, we get the true time from the RTC, so the
    // data is recorded accurately.
    //if (counter >= 37)
    if (counter >= 3)        
        {  
            ///*  DEBUGGING
            //Serial.begin (115200);
            Serial.println();
            Serial.print ("Checking time at: ");
            Serial.print (millis ());
            Serial.print(" -- ");
            digitalWrite(POWER, HIGH);
            pinMode(chipSelect, OUTPUT);
            // need to wait for the sensor to be ready here:
            delay(100);
            sensors.requestTemperatures(); // Send the command to get temperatures
            
            tm = RTC.get();
            SD.begin(chipSelect);
            logfile = SD.open(fname, FILE_WRITE);
            Serial.println (millis ());
            Serial.print(minute(tm), DEC);
            Serial.print(":");
            Serial.println(second(tm), DEC);

            Serial.print("Temp: ");
            Serial.println(sensors.getTempC(tc26));
            
            logfile.print(hour(tm), DEC);
            logfile.print(":");
            logfile.print(minute(tm), DEC);
            logfile.print(":");
            logfile.println(second(tm), DEC);
            logfile.print("Temp: ");
            logfile.println(sensors.getTempC(tc26));

            logfile.flush();
            logfile.close();

            pinMode(chipSelect, INPUT);
            digitalWrite(POWER, LOW);            
            //*/

            /*
            powerOnPeripherals ();
            getTime ();
            if (now.minute () != lastMinute)
                {
                    if ((now.minute () % TAKE_READINGS_EVERY) == 0)
                        recordReading ();
                    lastMinute = now.minute ();
                }  // end of change in the minute
            */
                
            ///*
            Serial.print ("Powering off at: ");
            Serial.println (millis ());
            //Serial.flush ();
            //Serial.end ();
            //*/
    
            //powerOffPeripherals ();
      
            counter = 0;
        }  // end of sleeping for 6 times
    Serial.flush ();
    Serial.end ();
    
}  // end of loop

void error(char *str)
{
  Serial.print("error: ");
  Serial.println(str);
  // red LED indicates error
  //digitalWrite(redLEDpin, HIGH);
  while (1);
}
