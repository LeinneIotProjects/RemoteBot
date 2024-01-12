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
#include <IRrecv.h>
#include <IRac.h>
#include <IRutils.h>

#include "web.h"
#include "wifi.h"
#include "utils.h"
#include "dht11.h"
#include "storage.h"
#include "websocket.h"
#include "safe_queue.h"

using namespace std;

struct RemoteData{
    stdAc::state_t acData;
};
SafeQueue<RemoteData> remoteQueue;

static void webSocketHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
    if(eventId == WEBSOCKET_EVENT_DATA && data->op_code == BINARY && data->data_len > 2){
        switch(data->data_ptr[0]){
            case 0x01: // a/c
                stdAc::state_t acData;
                if(data->data_ptr[1] < 1 || data->data_ptr[1] > kLastDecodeType){
                    cout << "[Socket] 잘못된 A/C 프로토콜 번호입니다\n";
                    break;
                }
                acData.protocol = (decode_type_t) data->data_ptr[1];
                acData.power = data->data_ptr[2];
                if(acData.power){
                    acData.mode = (stdAc::opmode_t) data->data_ptr[3];
                    acData.degrees = data->data_ptr[4];
                    acData.fanspeed = (stdAc::fanspeed_t) data->data_ptr[5];
                    cout << "모드: " << (int) acData.mode << ", 온도: " << (int) acData.degrees << ", 풍속: " << (int) acData.fanspeed << "\n";
                }
                remoteQueue.push({
                    .acData = acData,
                });
                break;
        }
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
    /*decode_results results;
    IRrecv irrecv(GPIO_NUM_8, 1024, 50, true);
    irrecv.setUnknownThreshold(12);
    irrecv.enableIRIn();*/

    //int64_t ir = 0;
    int64_t temp = -9999999;
    for(;;){
        if(!remoteQueue.empty()){
            auto data = remoteQueue.pop();
            if(data.acData.protocol != decode_type_t::UNKNOWN){
                ac.sendAc(data.acData);
                cout << "[A/C] power" << (data.acData.power ? "on" : "off") << "\n";
            }else{
                // TODO: remote
            }
        }

        if(!ws::connectServer){
            temp = -9999999;
        }else if(millis() - temp >= 20000){
            temp = millis();
            if(!ws::sendTemperature()){
                temp -= 19000;
                cout << "[DHT11] 전송에 실패했습니다. 1초 후 다시 시도합니다.\n";
            }
        }

        /*if(irrecv.decode(&results)){
            if (results.overflow)
                printf(D_WARN_BUFFERFULL "\n", 1024);

            cout << resultToHumanReadableBasic(&results).c_str();
            String description = IRAcUtils::resultAcToString(&results);
            if(description.length())
                cout << "설명: " << description.c_str() << "\n";
            cout << resultToSourceCode(&results).c_str() << "\n";
        }*/
    }
}

extern "C" void app_main(){
    uint8_t i = 0;
    TaskHandle_t handles[3];
    xTaskCreatePinnedToCore(wifiTask, "wifi", 10000, NULL, 1, &handles[i++], 0);
    xTaskCreatePinnedToCore(gpioTask, "gpio", 10000, NULL, 1, &handles[i++], 1);

    for(;;);
}