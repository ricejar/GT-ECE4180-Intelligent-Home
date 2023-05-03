#include "Thread.h"
#include "mbed.h"
#include "SongPlayer.h"
#include "rtos.h"
#include "SDFileSystem.h"
#include "uLCD_4DGL.h"
#include "HTU21D.h"
#include <cctype>
#include <ctime>
#include <iostream>   // std::cout
#include <string>     // std::string, std::to_string

Serial blue(p13,p14); // p28,p27 tx,rx
uLCD_4DGL LCD(p9,p10,p11); // ,RES
HTU21D temphumid(p28, p27); //Temp humid sensor || SDA, SCL
Mutex lcd_mutex;
Mutex alarm_set_mutex;

AnalogOut DACout(p18);
//On Board Speaker on Application board but very low volume using PWM

// Light sensor analog read
AnalogIn photocell(p15);
PwmOut myled(LED1);
PwmOut myled2(LED2);
PwmOut myled3(LED3);
PwmOut myled4(LED4);

// Temperature
volatile int sample_ftemp;
// Humidity
volatile int sample_humid;
// Alarm flag 
volatile int alarm_set = 0;
// Time for clock alarm
volatile int clock_time;
volatile bool help_flag = 0;
float light_level;
float blue_num;
char blue_char;
volatile char light_mode = '0';

SongPlayer mySpeaker(p26);
PwmOut speaker(p21);

// Debug Thread ensure mbed working
void mbed_LED(void const *args) {
    while(1) {
        myled4 = 1.0;
        myled3 = 0.0;
        Thread::wait(500);
        myled4 = 0.0;
        myled3 = 1.0;
        Thread::wait(500);
    }
}

// Temprature sensor
// Set temp alarm when temperature is higher than 74 F
void TempHum(void const *args) {
    bool temp_alarm = 0;
    while(1) {
        // measure temp and humidity
        sample_ftemp = temphumid.sample_ftemp();
        sample_humid = temphumid.sample_humid();
        // Check temp change
        if (sample_ftemp < 75 && temp_alarm == 1){
            temp_alarm = 0;
            lcd_mutex.lock();
            LCD.locate(0, 8);
            LCD.color(WHITE);
            LCD.printf("temp normal ");
            LCD.color(GREEN);
            lcd_mutex.unlock();
        } else if(sample_ftemp >= 75 && temp_alarm == 0){
            // Set temp alarm
            temp_alarm = 1;
            lcd_mutex.lock();
            LCD.locate(0, 8);
            LCD.color(RED);
            LCD.printf("HIGH TEMP!!!");
            LCD.color(GREEN);
            blue.puts("TEMPERATURE HIGH!!!\n");
            lcd_mutex.unlock();
            alarm_set_mutex.lock();
            alarm_set = 2;
            alarm_set_mutex.unlock();
        }
        Thread::wait(500);
    }
}

// LCD Thread update the screen
void LCD1(void const *args) {
    while(true) {       // thread loop
        lcd_mutex.lock();
        LCD.locate(0,1);
        LCD.printf("Temperature: %d F", sample_ftemp);
        LCD.locate(0,2);
        LCD.printf("Humidity: %d", sample_humid);
        LCD.locate(0,3);
        LCD.printf("light: %f", light_level);
        lcd_mutex.unlock();
        Thread::wait(1000);
    }
}

// Help Thread
void instruction_send(void const *args) {
    while(1){
        while(help_flag == 0){
            Thread::yield();
        }
        lcd_mutex.lock();
        blue.puts("Instructions:\n");
        Thread::wait(50);
        blue.puts("!a + number to set an alarm rings after seconds\n");
        Thread::wait(50);
        blue.puts("Example: !a10 for ring after 10 second\n\n");
        Thread::wait(50);
        blue.puts("!l0 for led follow room brightness\n");
        Thread::wait(50);
        blue.puts("!l1 turn on LED, !l2 turn off LED\n\n");
        Thread::wait(50);
        blue.puts("!b for light level\n");
        Thread::wait(50);
        blue.puts("!w for temperature and humidity\n\n");
        Thread::wait(50);
        blue.puts("Tmperature higher than 74 F, siren alarm will ring\n");
        lcd_mutex.unlock();
        help_flag = 0;
    }
}

/* 
Light Thread, Three light mode control by bluetooth thread
mode 0 follow light sensor reading
mode 1 turn on, mode 2 turn off
*/
void photo(void const *args) {
    char previous_light = '0';
    while(1) {
        switch(light_mode){
        case '0':
            light_level = photocell;
            myled = 1.0 - light_level;
            if (previous_light != light_mode){
                lcd_mutex.lock();
                blue.puts("LED follows room light level\n");
                lcd_mutex.unlock();
                previous_light = light_mode;
            }
            break;
        case '1':
            myled = 1.0;
            if (previous_light != light_mode){
                lcd_mutex.lock();
                blue.puts("LED turns on\n");
                lcd_mutex.unlock();
                previous_light = light_mode;
            }
            break;
        case '2':
            myled = 0.0;
            if (previous_light != light_mode){
                lcd_mutex.lock();
                blue.puts("LED turns off\n");
                lcd_mutex.unlock();
                previous_light = light_mode;
            }
            break;
        default:
            break;
        }
        Thread::wait(200);
    }
}

/*
Bluetooth Thread, Read data from bluefruit app on cellphone
Present message send from phone
process instruction start with '!'
Send back data for sensor data request
Process clock alarm time/light mode with digit
*/
void bluetooth_read(void const *args){
    int count = 0; 
    bool help_ins = 0;
    bool ins_flag = 0;
    while(1) {
        ins_flag = 0;
        char rec[20] = {};
        for (int i=0; i<20; i++){
                rec[i] = ' ';
            }
        while(!blue.readable())
        {
            count = 0;
            Thread::yield();
        }
        lcd_mutex.lock();
        LCD.locate(0, 10);
        for (int i=0; i<20; i++){
            LCD.printf(" ");
        }
        while(blue.readable()){ 
            rec[count] = blue.getc();
            if (count == 0 && rec[0] == '!'){
                ins_flag = 1;
            }
            count++;
        }
        if(ins_flag){
            char numc[18] = {};
            int invalid_input = 0;
            switch (rec[1]) // Check the second char for instruction
            {
            case 'a':   // clock alarm read
                for (int i = 2; i< count; i++){
                    if (rec[i] > 47 && rec[i] < 58){
                        numc[i-2] = rec[i];
                        continue;
                    }else if(i == 2){
                        invalid_input =1;
                        break;
                    }else{
                        break;
                    }
                }
                if (count <= 2){    // no number
                    invalid_input = 1;
                }
                if (invalid_input == 1){
                    LCD.locate(0,10);
                    LCD.printf("Invalid input!!");
                    blue.puts("Invalid Instruction\n");
                }else{  // Read the number
                    clock_time = atoi(numc);
                    char buf3[40] = {};
                    sprintf(buf3,"Set an alarm after %d seconds\n",clock_time);
                    blue.puts(buf3);
                    alarm_set_mutex.lock();
                    alarm_set = 1;
                    alarm_set_mutex.unlock();
                    LCD.locate(0, 10);
                    LCD.printf("Alarm ins!     ");
                }
                break;
            case 'l':   /// Light mode instruction
                invalid_input = 0;
                if (count <= 2){
                    invalid_input = 1;
                }else if(rec[2] < 0x30 || rec[2] > 0x32){
                    invalid_input = 1;
                }else{
                    light_mode = rec[2];
                    LCD.locate(0, 10);
                    LCD.printf("Light ins!     ",clock_time);
                }
                if (invalid_input == 1){
                    LCD.locate(0,10);
                    LCD.printf("Invalid input!!");
                    blue.puts("Invalid Instruction!!\n");
                }
                break;
            case 'h':   // Help instruction
                help_ins = 1;
                break;
            case 'w':   // Temp and humidity data request
                char buf1[40] = {};
                sprintf(buf1,"Temperature: %d F\n",sample_ftemp);
                blue.puts(buf1);
                char buf2[40] = {};
                sprintf(buf2,"Humidity %d \n",sample_humid);
                blue.puts(buf2);
                LCD.locate(0, 10);
                LCD.printf("temp ins!      ");
                break;
            case 'b':   // Light data request
                char buf[40] = {};
                sprintf(buf,"light level: %2.3f%%\n",light_level * 100);
                blue.puts(buf);
                LCD.locate(0, 10);
                LCD.printf("light ins!     ");
                break;
            default:    // Instruction does not exist
                blue.puts("Invalid instruction!!\n");
                break;
            }    
        }else{  // Print message from phone on LCD
            LCD.locate(0, 13);
            LCD.printf(rec);
        }
        lcd_mutex.unlock();
        if (help_ins == 1){
            help_flag = 1;
            help_ins = 0;
        }    
    }
}


int main() {
    // Set up LCD
    LCD.locate(0,0);
    LCD.color(WHITE);
    LCD.printf("send !h for help");
    LCD.locate(0,12);
    LCD.printf("MSG from phone:");
    LCD.color(GREEN);
    Thread thread1(TempHum); //Start LED effect 
    Thread thread2(instruction_send);
    Thread thread3(LCD1); //start thread1
    Thread thread5(mbed_LED);
    Thread thread6(bluetooth_read);
    Thread thread7(photo);

    int i;
    int tmp_alarm = 0;
    // generate a 500Hz tone using PWM hardware output
    speaker.period(1.0/500.0); // 500hz period
    // turn off audio

    /*
    Main Thread
    Control the Speaker, Determine which type of sound to play
    Sent and countdown for clock alarm
    ring the siren alarm for temp alarm
    */
    while(1) {       // thread loop
        while(alarm_set == 0){
            Thread::yield();
        }
        alarm_set_mutex.lock();
        switch (alarm_set) {
            case 1: // Set by bluetooth thread clock alarm
                for (i = clock_time; i > 0; i--){   // count down
                    lcd_mutex.lock();
                    LCD.locate(0,6);
                    LCD.color(BLUE);
                    LCD.printf("Ring after %d sec ",i);
                    LCD.color(GREEN);
                    lcd_mutex.unlock();
                    Thread::wait(1000.0);
                }
                lcd_mutex.lock();
                LCD.locate(0,6);
                LCD.color(BLUE);
                LCD.printf("Ringing          ");
                LCD.color(GREEN);
                lcd_mutex.unlock();
                speaker.period(1.0/500.0);
                // Beeping when time up
                for (i=0; i<4 ;i++){
                    speaker =0.5; //50% duty cycle - max volume
                    Thread::wait(250.0);
                    speaker=0.0; 
                    Thread::wait(250.0);
                }
                break;
            // Temperature alarm.
            case 2:
                for (i=0; i<28; i=i+4) {
                    speaker.period(1.0/969.0);
                    speaker = float(i)/50.0;
                    Thread::wait(250.0);
                    speaker.period(1.0/800.0);
                    Thread::wait(250.0);
                }
                speaker = 0.0;
                break;
            default:
                break;
        };
        // Clear the flag
        tmp_alarm = alarm_set;  // record current alarm type
        alarm_set = 0;
        alarm_set_mutex.unlock();
        lcd_mutex.lock();
        LCD.locate(0,6);
        LCD.color(BLUE);
        // Print data on LCD
        switch (tmp_alarm) {
        case 1:
            LCD.printf("Stopwatch Alarm  ");
            break;
        case 2:
            LCD.printf("Temp alarm       ");
            break;
        default:
            break;
        }
        LCD.color(GREEN);
        lcd_mutex.unlock();
    }
    
}
