#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <driver/touch_sensor.h>
#include <atomic>
#include <iostream>
#include <utility>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>

#include "web.h"
#include "wifi.h"
#include "utils.h"
#include "dht11.h"
#include "storage.h"
#include "websocket.h"

using namespace std;

static void webSocketHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
    if(eventId == WEBSOCKET_EVENT_DATA && data->op_code == BINARY){
        // TODO: send ir data
    }
}

static void wifiHandler(void* arg, esp_event_base_t base, int32_t id, void* data){
    if(id == WIFI_EVENT_AP_START){
        web::start();
    }else if(web::stop()){
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
}

static void wifiTask(void* args){
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    storage::begin();
    wifi::begin();
    ws::start(webSocketHandler);

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiHandler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &wifiHandler, NULL);

    for(;;){
        int64_t time = millis();
        while(!wifi::connect){
            if(
                wifi::getMode() != WIFI_MODE_APSTA &&
                millis() - time >= 5 * 1000
            ){
                wifi::setApMode();
            }
            continue;
        }
        
        time = millis();
        while(!ws::connectServer){
            if(!ws::isConnected() || millis() - time < 500){
                continue;
            }
            time = millis();
            ws::sendWelcome();
        }
    }
}

static void gpioTask(void* args){
    IRac ac(GPIO_NUM_7);
    int64_t ir = 0;
    int64_t temp = -10000;
    for(;;){
        if(ws::connectServer && millis() - temp >= 30000){
            temp = millis();
            ws::sendTemperature();
        }
        
        /*if(millis() - ir >= 4000){
            ir = millis();

            ac.next.protocol = decode_type_t::COOLIX;
            ac.next.mode = stdAc::opmode_t::kCool;
            ac.next.fanspeed = stdAc::fanspeed_t::kMax;
            ac.next.degrees = 26;
            ac.next.power = !ac.next.power;
            cout << "power" << (ac.next.power ? "on" : "off") << "\n";
            ac.sendAc();
        }*/
    }
}

extern "C" void app_main(){
    uint8_t i = 0;
    TaskHandle_t handles[2];
    xTaskCreatePinnedToCore(wifiTask, "wifi", 10000, NULL, 1, &handles[i++], 0);
    xTaskCreatePinnedToCore(gpioTask, "gpio", 10000, NULL, 1, &handles[i++], 1);

    for(;;);
}