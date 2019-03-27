/*
    #include <WiFi.h>
    #include <ezTime.h>
    #define CONF_TIMEZONE "Europe/Stockholm"
    const char *ssid     = "DogeSuchWOW";
    const char *password = "doge is love doge is life";

    void setup() {
        Serial.begin(115200);
        Serial.println("Serial run");
        WiFi.begin(ssid, password);
        while( WiFi.status() != WL_CONNECTED ){
            delay( 500 );
            Serial.print ( "." );
        }
        
        Timezone tz = new Timezone();
        Timezone tempTime = new Timezone();

        waitForSync();
        Serial.println("Time: "+tz.dateTime());
        tempTime.setTime(
            12, // H 
            0,  // M
            0,  // S
            1,1,2018
        );
        Serial.println("tz after setting tempTime");
        Serial.println(tz.dateTime());
    }

    void loop(){ delay(1000); }
*/

/*
    // Clock test
    #include <WiFi.h>
    #include <ezTime.h>
    const char *ssid     = "DogeSuchWOW";
    const char *password = "doge is love doge is life";
    Timezone tz;

    void setup() {
        Serial.begin(115200);
        Serial.println("Serial run");
        WiFi.begin(ssid, password);
        while( WiFi.status() != WL_CONNECTED ){
            delay( 500 );
            Serial.print ( "." );
        }
        
        waitForSync();
        tz.setLocation(F("Europe/Stockholm"));
        Serial.println("Time: "+tz.dateTime());
        Serial.printf("Setting clock to 02:00:00 on YMD %i-%i-%i\n", tz.year(), tz.month(), tz.day());

        long diff = tz.now()-now();
        time_t startTime = makeTime(2, 0, 0, tz.day(), tz.month(), tz.year())+diff;
        Serial.println(startTime);
        Serial.println("tz after setting tempTime");
        Serial.println(tz.dateTime(startTime, LOCAL_TIME));
    }

    void loop(){ delay(1000); }
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_task.h> 

#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"




void setup() {
    Serial.begin(115200);
    Serial.printf("Serial running on core %i\n", xPortGetCoreID());

    pinMode(speakerPin,OUTPUT);

    xTaskCreatePinnedToCore(
        audioTask,   // Function to implement the task 
        "audioTask", // Name of the task 
        1024,      // Stack size in words 
        NULL,       // Task input parameter 
        1,          // Priority of the task 
        NULL,       // Task handle. 
        0           // Core where the task should run 
    );  
    
 

}

void loop(){ 

    Serial.printf("Tick on core %i\n", xPortGetCoreID());
    delay(2000);

}