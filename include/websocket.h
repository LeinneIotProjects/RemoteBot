#pragma once

#include <atomic>
#include <dht11.h>
#include <driver/gpio.h>
#include <esp_http_client.h>
#include <esp_websocket_client.h>

#include "wifi.h"
#include "dht11.h"
#include "utils.h"
#include "storage.h"

#define WEBSOCKET_URL "ws://localhost:8080/iot"

typedef enum{
    CONTINUITY,
    STRING,
    BINARY,
    QUIT = 0x08,
    PING,
    PONG
} websocket_opcode_t;

namespace ws{
    atomic<bool> connectServer(false);
    esp_websocket_client_handle_t webSocket = NULL;

    void sendWelcome(){
        auto device = storage::getDeviceId();
        uint8_t buffer[device.length() + 2] = {
            0x01, // protocol type (0x01: welcome)
            0x03, // [data] device type(0x01: checker, 0x02: switch bot, 0x03: remote bot)
        };
        for(uint8_t i = 0; i < device.length(); ++i){
            buffer[2 + i] = device[i];
        }
        if(esp_websocket_client_send_bin(webSocket, (char*) buffer, device.length() + 2, portMAX_DELAY) == device.length() + 2){
            printf("[WS] Send welcome message\n");
        }
    }

    bool sendTemperature(){
        uint8_t err = 0;
        uint8_t buffer[6] = {0x04}; // protocol type (0x04: temperature)
        while(DHT11::read(GPIO_NUM_9, &buffer[1]) != ESP_OK){
            if(++err > 3){
                return false;
            }

            cout << "[DHT11] 데이터가 올바르지 않습니다. 다시 시도합니다.\n";
            for(uint8_t i = 0; i < 5; ++i){
                buffer[i + 1] = 0;
            }
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }
        esp_websocket_client_send_bin(webSocket, (char*) buffer, 5, portMAX_DELAY);
        return true;
    }

    bool isConnected(){
        return esp_websocket_client_is_connected(webSocket);
    }

    static void eventHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
        esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
        if(eventId == WEBSOCKET_EVENT_DISCONNECTED){
            if(connectServer){
                printf("[WS] Disconnected WebSocket\n");
            }
            connectServer = false;
        }else if(eventId == WEBSOCKET_EVENT_ERROR){
            if(!wifi::connect){
                return;
            }
            cout << "[Socket] 에러 발생, type: NONE\n";
        }else if(eventId == WEBSOCKET_EVENT_DATA){
            if(data->op_code == STRING && !connectServer){
                string device(data->data_ptr, data->data_len);
                if(storage::getDeviceId() == device){
                    connectServer = true;
                    printf("[WS] Connect successful.\n");
                }else{
                    printf("[WS] FAILED. device: %s, receive: %s, len: %d\n", storage::getDeviceId().c_str(), device.c_str(), data->data_len);
                }
            }
        }
    }

    void start(esp_event_handler_t handler){
        esp_websocket_client_config_t config = {
            .uri = WEBSOCKET_URL,
            .keep_alive_enable = true,
        };

        while(webSocket == NULL){
            webSocket = esp_websocket_client_init(&config);
        }
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, handler, NULL);
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, eventHandler, NULL);

        esp_err_t err = ESP_FAIL;
        while(err != ESP_OK){
            err = esp_websocket_client_start(webSocket);
        }
    }
}