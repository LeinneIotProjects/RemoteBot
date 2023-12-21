#pragma once

#include <iostream>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace std;

namespace DHT11{
    static esp_err_t _waitOrTimeout(gpio_num_t gpio, uint16_t micros, int level){
        uint16_t tick = 0;
        while(gpio_get_level(gpio) == level){ 
            if(tick++ > micros){
                return ESP_FAIL;
            }
            ets_delay_us(1);
        }
        return tick;
    }

    static esp_err_t _checkResponse(gpio_num_t gpio){
        if(_waitOrTimeout(gpio, 80, 0) == ESP_FAIL){
            return ESP_FAIL;
        }
        if(_waitOrTimeout(gpio, 80, 1) == ESP_FAIL){
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    esp_err_t read(gpio_num_t gpio, uint8_t* data){
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, 0);
        ets_delay_us(20 * 1000);
        gpio_set_level(gpio, 1);
        ets_delay_us(40);
        gpio_set_direction(gpio, GPIO_MODE_INPUT);

        if(_checkResponse(gpio) != ESP_OK){
            cout << "[DHT11] 반응 없음\n";
            return ESP_FAIL;
        }
        for(int i = 0; i < 40; i++){
            if(_waitOrTimeout(gpio, 50, 0) == ESP_FAIL){
                cout << "[DHT11] 시간 초과\n";
                return ESP_FAIL;
            }
            if(_waitOrTimeout(gpio, 70, 1) > 28){
                data[i / 8] |= (1 << (7 - (i % 8)));
            }
        }
        return (data[4] == (data[0] + data[1] + data[2] + data[3])) ? ESP_OK : ESP_FAIL;
    }
}