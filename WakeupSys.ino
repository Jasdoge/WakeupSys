/*
    Usage:
    - Plug in, the clock will start blinking 00:00, this means you can connect to the wifi manager and hook it up to wifi
    - You can reconnect to wifi by pressing and holding both buttons for 2 seconds
    - Pressing a button enters time set mode. This sets when the clock should start lighting up.
    - Pushing a button in this mode adds or subtracts 5 min. Pressing and holding will rapidly start adding in 15 min intervals.
    - Tapping both buttons at the same time will set the start time to 8h from the moment of when you tapped it.
    - The clock will light up over 30 min, then hold max for 30 min, then fade out over 30 min.

    Compiling:
    - Change CONF_TIMEZONE to yours
    - Compile


*/
#define CONF_TIMEZONE "Europe/Stockholm"
const char* AP_NAME = "Jasdoge WakeupSys";

// Include the required libraries. 
#include "esp_task.h"
#include "SPIFFS.h"
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include <WiFi.h>
#include <ESP32Ticker.h>

// You should be able to get these from the arduino IDE
#include <TM1637Display.h>
#include <ezTime.h>
#include <WiFiManager.h>


// Config
const uint8_t lamps_pin = 21;               // GPIO going to the gate of the MOSFET
const uint8_t lamps_chan = 0;               // Used for ledc, you don't need to change this

const uint8_t button_l_pin = 14;            // Left button GPIO
const uint8_t button_r_pin = 12;            // Right button GPIO

const uint8_t clock_brightness = 0xA;       // Brightness of the clock, I think 0xF is max

const int speakerPin = 5;
const int freq = 75;    // microseconds

// Display
const uint8_t disp_clk_pin = 23;                        // GPIO for the clock clock pin
const uint8_t disp_dio_pin = 19;                        // GPIO for the clock data i/o pin
TM1637Display display(disp_clk_pin, disp_dio_pin);      // Clock object

// Clock
int8_t fade_in_start_h = 2;                             // overwritten when booted, changing here does nothing
int8_t fade_in_start_m = 30;                            // overwritten when booted, changing here does nothing
const int fade_in_duration = 30;                        // Fade in duration in minutes
const int fade_hold_time = 30;                          // Hold time after peaking in minutes
const int fade_out_time = 30;                           // Time to fade out after holding

// interface
bool button_l_pressed = false;                          // These hold the button press state
bool button_r_pressed = false;

// Timers
long lastClockUpdate = 0;                               // Holds the time the clock was last updated, making sure it doesn't need to update needlessly often
long mode_time_set_entered = 0;                         // If > 0, then we're in time setting mode.
long mode_time_last_auto_step = 0;                      // When clicking and holding, this holds the time when the last add or subtraction took place
long mode_time_set_released = 0;                        // Time set when you release both buttons, checking when the display should go back to current time

Ticker clockTicker;                                     // Timer
bool clock_vis = false;                                 // Boolean for the middle dots visibility state
bool wifi_man_running = false;                          // Wifi manager run state
long last_clock_update = 0;                             // Time when the clock was last _physically_ updated. If you send updates to the clock too often it borks.
long update_clock_after = 0;                            // Set when you try to update the clock too often, makes sure it updates as soon as it can if you try to update it too soon.
long double_press_start = 0;                            // Time when you pushed and held both buttons at the same time.

Timezone tz;                                            // Holds the time

// Gets the unix timestamp of when the lamp should start fading in
time_t getStartFadeInUnix(){

    // Get active time
    time_t cur = tz.now();
    // Get the start fade time this active day
    time_t next = makeTime(fade_in_start_h, fade_in_start_m, 0, tz.day(), tz.month(), tz.year());
    
    // Get the total program duration
    int programDur = (fade_in_duration+fade_hold_time+fade_out_time)*60;
    // Get the nr of seconds in a day
    const int day = 3600*24;
    
    // Get the start time of the previous day
    time_t pre = next-day+programDur;   // Previous day
    
    //Serial.println(tz.dateTime());
    //Serial.printf("Cur %i, Next %i, Pre %i\n", cur, next, pre);
    //Serial.printf("Offs: %i\n", offs);

    // If the previous day's cycle is still active, use that. This happens if you set the fade in time at 23:30 or so
    if( next > cur && cur < pre ){
        return next-day;
    }
    // If today's cycle has ended, pick tomorrow's
    else if( cur > next+programDur )
        next = makeTime(fade_in_start_h, fade_in_start_m, 0, tz.day()+1, tz.month(), tz.year());
    
    return next;

}

// Starts the wifi manager
void startWifiManager( bool autoConnect = true ){

    WiFiManager wifiManager;
    wifi_man_running = true;
    updateClock();
    //Serial.printf("Starting wifi manager autoConnect: %i\n", autoConnect);
	if( autoConnect )
		wifiManager.autoConnect(AP_NAME);
	else
		wifiManager.startConfigPortal(AP_NAME);
    //Serial.printf("Wifi manager stopped\n");
    wifi_man_running = false;

}

// Updates the time on the display
void updateClock(){

    // Limits how often updates can be sent, because otherwise the clock derps out
    long ms = millis();
    if( last_clock_update+20 > ms ){
        update_clock_after = last_clock_update+20;
        return;
    }
    last_clock_update = ms;

    // Fire up ze display
    uint8_t data[4] = { 0 };
    //Serial.println("Time: "+tz.dateTime());
    //Serial.printf("%i:%i\n", tz.hour(), tz.minute());
    
    // Default to the time when it should start fading in the light
    int8_t h = fade_in_start_h;
    int8_t m = fade_in_start_m;

    // If wifi manager is running, it sets 00:00
    if( wifi_man_running ){
        h = 0;
        m = 0;
    }
    // If we're not running the wifi manager or in fade time set mode, use the current real time
    else if( !mode_time_set_entered ){
        h = tz.hour();
        m = tz.minute();
    }

    // This if statement makes the 00:00 blink in wifi mode
    if( !wifi_man_running || clock_vis ){    
        data[0] = display.encodeDigit((h-h%10)/10);
        data[1] = display.encodeDigit(h%10) | (clock_vis ? 0x80 : 0);
        data[2] = display.encodeDigit((m-m%10)/10);
        data[3] = display.encodeDigit(m%10);
    }


    display.setSegments(data);

}


// Ticker event run every 250 ms
void clockTickerEvt(){

    // When showing the real time, ignore this
    if( !mode_time_set_entered && !wifi_man_running ){
        clock_vis = true;
        return;
    }

    // When not showing real time, flash the colon or numbers
    clock_vis = !clock_vis;
    updateClock();

}


// Handle the POWER LAMPS
void updateLamp(){

    // Current unix time
    long now = tz.now();
    // Light intensity between 0 and 1
    float intens = 0;

    // Get the times
    long startFade = getStartFadeInUnix();              // Time when to start fading in
    long endFade = startFade+fade_in_duration*60;       // Time when fade in has reached peak
    long startFadeOut = endFade+fade_hold_time*60;      // Time when we should start fading out
    long endFadeOut = startFadeOut+fade_out_time*60;    // Time when it should have finished fading out

    //Serial.println(tz.dateTime());
    //Serial.printf("Now %i, startfade %i, endfade offs %i, startFadeOut offs %i, endFadeOut offs %i\n", now, startFade, endFade-startFade, startFadeOut-endFade, endFadeOut-startFadeOut);
    
    // We're currently fading in, get the value based on the time between start and finish
    if( now >= startFade && now < endFade ){
        int dur = endFade-startFade;
        intens = 1.0-(endFade-now)/(float)dur;
        //Serial.printf("Setting intensity on fade in %f \n", t);
    }
    // We're currently in the hold time, just return max
    else if( now >= endFade && now < startFadeOut )
        intens = 1;
    // We're fading out, so get the time between the end start and end fade end
    else if( now >= startFadeOut && now < endFadeOut ){
        int dur = endFadeOut-startFadeOut;
        intens = (endFadeOut-now)/(float)dur;
    }

    // Convert into a 10 bit value
    uint16_t intensity = round(1023*pow(intens,2));
    
    // output to PWM
    ledcWrite(lamps_chan, intensity);
    //Serial.println(intensity);

}

// Exit time setting mode
void closeTimeMode(){
    
    // Reset the button pressed times
    mode_time_set_released = mode_time_set_entered = 0;
    // Set the current time
    updateClock();
    // Save configuration
    saveConfig();

}

// Saves configuration to SPIFFS
void saveConfig(){

    File f = SPIFFS.open("/conf", "w");
    if( f ){
        // We're storing the bytes as bytes, not as text
        char out[3] = {fade_in_start_h, fade_in_start_m};
        f.print(out);
        //Serial.printf("Saved config %i, %i\n", out[0], out[1]);
        f.close();
    }
    else
        Serial.println("Unable to write config");

}

// This is run every 100ms or so with the current button states
void updateButtons( bool l, bool r ){

    // Current script time
    long ms = millis();

    // Boolean which is true if any button has been pressed or released since last check
    bool press_change = l != button_l_pressed || r != button_r_pressed;

    //Serial.printf("L %i, R %i\n", l, r);

    // Both buttons are pressed
    if( l && r ){
        // Hold 2 sec to start wifi manager
        if( ! double_press_start )
            double_press_start = ms;
        // After 2 sec, start the wifi manager
        else if( ms-double_press_start > 2000 ){
            double_press_start = 0;
            closeTimeMode();
            startWifiManager(false);
        }
        return;
    }
    // We have pressed and released both buttons at the same time within 2 sec
    else if( double_press_start ){
        
        // Automatically set time to 8h ahead rounded down to the nearest 5 min
        fade_in_start_h = tz.hour()+8;
        if( fade_in_start_h > 23 )
            fade_in_start_h = fade_in_start_h-24;
        uint8_t m = tz.minute();
        fade_in_start_m = m-m%5;
        updateClock();
        saveConfig();
        double_press_start = 0;
        return;

    }

    // No buttons are pressed
    if( !l && !r ){

        // No buttons are pressed, and we're not in time set mode
        // So just return, nothing to see here
        if( !mode_time_set_entered )
            return;

        // We just released a button after pressing one
        mode_time_last_auto_step = 0;       // Stop auto cycling if the button was pressed for a long time

        // 3 sec has passed since the last button press, return to showing current time
        if( ms > mode_time_set_released+3000 && mode_time_set_released )
            closeTimeMode();
        // We just released the button, set so it returns to current time after 3 sec
        else if( !mode_time_set_released )
            mode_time_set_released = ms;

        // No need to continue, we've handled no buttons pressed
        return;

    }

    // On first press
    if( !mode_time_set_entered ){
        //Serial.println("Entered pressed mode");
        mode_time_set_entered = ms; // Enter time set mode
        updateClock();
        return;
    }

    // Any press after first
    mode_time_set_released = 0;                 // We have not released a button yet
    if( !mode_time_last_auto_step )
        mode_time_last_auto_step = ms+750;      // If this was the first press and hold, wait a bit before starting to cycle

    
    // Auto step or if this was a manual press
    if( ms > mode_time_last_auto_step+250 || press_change ){

        // Add or subtract minutes
        int minutes = press_change ? 5 : 15;        // 5 min on quick tap, 15 min on cycle
        if( l )
            minutes = -minutes;

        fade_in_start_m += minutes;

        // Limit to 24h format
        if( fade_in_start_m < 0 ){
            fade_in_start_m = fade_in_start_m+60;
            --fade_in_start_h;
        }
        else if( fade_in_start_m >= 60 ){
            fade_in_start_m = fade_in_start_m-60;
            ++fade_in_start_h;
        }
        
        if( fade_in_start_h < 0 )
            fade_in_start_h += 24;
        else if( fade_in_start_h >= 24 )
            fade_in_start_h -= 24;
        
        //Serial.printf("Setting time %i:%i\n", fade_in_start_h, fade_in_start_m);
        // Output to the display
        updateClock();

    }
    
    


}





/* initialize with any 32 bit non-zero  unsigned long value. */
#define LFSR_INIT  0xfeedfaceUL
/* Choose bits 32, 30, 26, 24 from  http://arduino.stackexchange.com/a/6725/6628
 *  or 32, 22, 2, 1 from 
 *  http://www.xilinx.com/support/documentation/application_notes/xapp052.pdf
 *  or bits 32, 16, 3,2  or 0x80010006UL per http://users.ece.cmu.edu/~koopman/lfsr/index.html 
 *  and http://users.ece.cmu.edu/~koopman/lfsr/32.dat.gz
 */  
#define LFSR_MASK  ((unsigned long)( 1UL<<31 | 1UL <<15 | 1UL <<2 | 1UL <<1  ))

unsigned int generateNoise(){ 
  // See https://en.wikipedia.org/wiki/Linear_feedback_shift_register#Galois_LFSRs
   static unsigned long int lfsr = LFSR_INIT;  /* 32 bit init, nonzero */
   /* If the output bit is 1, apply toggle mask.
                                    * The value has 1 at bits corresponding
                                    * to taps, 0 elsewhere. */

   if(lfsr & 1) { lfsr =  (lfsr >>1) ^ LFSR_MASK ; return(1);}
   else         { lfsr >>= 1;                      return(0);}
}

void audioTask( void * pvParameters ){

    pinMode(speakerPin,OUTPUT);
    Serial.printf("Audio task now running on core %i\n", xPortGetCoreID());
    while( true ){
        TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
        TIMERG0.wdt_feed=1;
        TIMERG0.wdt_wprotect=0;
        digitalWrite(speakerPin, generateNoise());
        delayMicroseconds(freq);
    }

}



void setup() {

    Serial.begin(115200);
    Serial.println("Serial run");
    
    // Filesystem start
    SPIFFS.begin();                       
    // Set the display brightness
    display.setBrightness(clock_brightness);

    // Setup the GPIOs
    pinMode(button_r_pin, INPUT);
    pinMode(button_l_pin, INPUT);
    pinMode(lamps_pin, OUTPUT);
    digitalWrite(lamps_pin, LOW);
    ledcSetup(lamps_chan, 8000, 10);
    ledcAttachPin(lamps_pin, lamps_chan);

    Serial.printf("Reading lamps %i %i\n", digitalRead(button_r_pin), digitalRead(button_l_pin));

    delay(100);
    
    // Print 1337 on the display (boot message)
    uint8_t data[4] = { 
        display.encodeDigit(1),
        display.encodeDigit(3),
        display.encodeDigit(3),
        display.encodeDigit(7)
    };
    display.setSegments(data);

    // Filesystem failed, can't go on. Fix your hardware.
    if( !SPIFFS.begin(true) ){
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    // load config from filesystem
    File f = SPIFFS.open("/conf", "r");
    if( f ){
        char a = f.read();      // First and second char are bytes representing hour and minute
        char b = f.read();

        // Limit it to 24h format        
        if( a < 0 )
            a = 0;
        if( b < 0 )
            b = 0;
        if( a > 23 )
            a = 23;
        if( b > 59 )
            b = 59;

        // Make sure the time is divisible by 5
        b = b-b%5;

        if( ~a && ~b ){
            fade_in_start_h = a;
            fade_in_start_m = b;
        }

        Serial.printf("Got time conf %i, %i\n", a, b);
        f.close();
    }
    else
        Serial.println("No config");

    // Connect to wifi, or if that fails, enter wifi config mode.
    startWifiManager();

    // Set display to 1337 again since wifi manager overrides the display
    display.setSegments(data);

    // Start up the audio loop
    xTaskCreatePinnedToCore(
        audioTask,   // Function to implement the task 
        "audioTask", // Name of the task 
        1024,      // Stack size in words 
        NULL,       // Task input parameter 
        1,          // Priority of the task 
        NULL,       // Task handle. 
        0           // Core where the task should run 
    );  

    // Sync the clock
    //Serial.print("setup() running on core ");
    //Serial.println(xPortGetCoreID());
    Serial.printf("Waiting for sync %i\n", millis());
    waitForSync();

    delay(3000);
    // Attempt to set the timezone
    Serial.printf("Setting timezone %i\n", millis());
    bool autoSet = tz.setLocation(F(CONF_TIMEZONE));
    if( !autoSet )
        Serial.printf("Failed to set timezone %i\n", millis());

    Serial.println("Timezone:");
    Serial.println(tz.getTimezoneName());
    Serial.println("Time: "+tz.dateTime());
    // Output to the display
    updateClock();

    // Setup the timer event
    clockTicker.attach_ms(250, clockTickerEvt);

}

void loop() {

    //Audio.setPitch(0, pitch);
    //vTaskDelay(100);

    // Current time
    long ms = millis();

    // Update the clock every 4 sec while we're not changing the fade in time
    if( (ms > lastClockUpdate+4000 || !lastClockUpdate) && !mode_time_set_entered ){
        
        lastClockUpdate = ms;
        waitForSync();          // syncs the time if needed
        updateClock();          // Updates the display
        updateLamp();           // Updates the lamps
    
    }

    // There's a scheduled display update, handle that
    if( ms > update_clock_after ){

        update_clock_after = 0;
        updateClock();

    }
    
    // Read and handle the button states
    bool l = digitalRead(button_l_pin);
    bool r = digitalRead(button_r_pin);
    updateButtons(l, r);    
    button_l_pressed = l;
    button_r_pressed = r;

    // Don't need to update more than every 100 ms
    // This also debounces the buttons
    delay(100);

}
