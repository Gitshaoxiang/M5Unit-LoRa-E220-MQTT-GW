
#include <M5Unified.h>
#include <M5GFX.h>
#include "M5_LoRa_E220.h"
#include <Arduino.h>

#include "lora_gw_bg.h"
#include "lora_node_bg.h"

#include "M5UnitENV.h"

SCD4X scd4x;
SHT4X sht4;
BMP280 bmp;

LoRa_E220 lora;
struct LoRaConfigItem_t config;
struct RecvFrame_t data;

#define DEVICE_LOCAL_ADDR 0x0002

/** prototype declaration **/
void LoRaRecvTask(void *pvParameters);
void LoRaSendTask(void *pvParameters);
void LoRaReportTask(void *pvParameters);
void ReadDataFromConsole(char *msg, int max_msg_len);

void print_log(String info);
void initSensor();
bool LoRaSendFrame(uint16_t addr, char *buffer, int len);

M5Canvas canvas(&M5.Display);

void gfx_canvas_init() {
    M5.Display.setFont(&fonts::Orbitron_Light_32);
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(WHITE);
    M5.Display.pushImage(0, 0, 320, 240, image_data_lora_node_bg);
    char addr[10] = {0};
    sprintf(addr, "0x%04X", DEVICE_LOCAL_ADDR);
    M5.Display.drawString(addr, M5.Display.width() / 2, 32);

    canvas.createSprite(294, 82);
    canvas.setTextSize(1);
    canvas.setTextScroll(true);
    canvas.pushSprite(13, 148);
}

void setup() {
    // put your setup code here, to run once:
    M5.begin();
    gfx_canvas_init();
    delay(1000);  // Serial init wait
    initSensor();
    lora.Init(&Serial2, 9600, SERIAL_8N1, 16, 17);

    // lora.Init();
    // lora.Init(&Serial2, 9600, SERIAL_8N1, 33, 32);

    lora.SetDefaultConfigValue(config);

    config.own_address              = DEVICE_LOCAL_ADDR;
    config.baud_rate                = BAUD_9600;
    config.uart_config              = UART_8N1;
    config.air_data_rate            = DATA_RATE_2_4Kbps;
    config.subpacket_size           = SUBPACKET_200_BYTE;
    config.rssi_ambient_noise_flag  = RSSI_AMBIENT_NOISE_ENABLE;
    config.transmitting_power       = TX_POWER_22dBm;
    config.own_channel              = 0x00;
    config.rssi_byte_flag           = RSSI_BYTE_ENABLE;
    config.transmission_method_type = UART_P2P_MODE;
    config.lbt_flag                 = LBT_DISABLE;
    config.wor_cycle                = WOR_2000MS;
    config.encryption_key           = 0x1234;
    config.target_address           = 0x0000;
    config.target_channel           = 0x00;

    if (lora.InitLoRaSetting(config) != 0) {
        // print_log("init error, pls pull the M0,M1 to 1");
        // print_log("or click Btn to skip");
        while (lora.InitLoRaSetting(config) != 0) {
            M5.update();
            if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
                break;
            }
        }
    }
    // print_log("init succeeded, pls pull the M0,M1 to 0");
    // print_log("Click Btn to Send Data");

    // マルチタスク
    xTaskCreateUniversal(LoRaRecvTask, "LoRaRecvTask", 8192, NULL, 1, NULL, APP_CPU_NUM);
    xTaskCreateUniversal(LoRaSendTask, "LoRaSendTask", 8192, NULL, 1, NULL, APP_CPU_NUM);
    xTaskCreateUniversal(LoRaReportTask, "LoRaReportTask", 8192, NULL, 1, NULL, APP_CPU_NUM);
}

void loop() {
    // put your main code here, to run repeatedly:
    delay(1000);
}

void LoRaReportTask(void *pvParameters) {
    char buffer[128] = {0};
    while (1) {
#if DEVICE_LOCAL_ADDR == 0x0001
        if (scd4x.update()) {
            Serial.println();

            Serial.print(F("CO2(ppm):"));
            Serial.print(scd4x.getCO2());

            Serial.print(F("\tTemperature(C):"));
            Serial.print(scd4x.getTemperature(), 1);

            Serial.print(F("\tHumidity(%RH):"));
            Serial.print(scd4x.getHumidity(), 1);

            Serial.println();
            memset(buffer, 0, sizeof(buffer));
            int len = sprintf(buffer, "Addr:%04X, CO2(ppm):%d", DEVICE_LOCAL_ADDR, scd4x.getCO2());
            LoRaSendFrame(0x0000, buffer, len);
        } else {
            Serial.print(F("."));
        }
        delay(1000 + random(1000, 2000));
#endif

#if DEVICE_LOCAL_ADDR == 0x0002

        if (sht4.update()) {
            Serial.println("-----SHT4X-----");
            Serial.print("Temperature: ");
            Serial.print(sht4.cTemp);
            Serial.println(" degrees C");
            Serial.print("Humidity: ");
            Serial.print(sht4.humidity);
            Serial.println("% rH");
            Serial.println("-------------\r\n");
            memset(buffer, 0, sizeof(buffer));
            int len =
                sprintf(buffer, "Addr:%04X, Temp:%.2f, Humidity:%.2f", DEVICE_LOCAL_ADDR, sht4.cTemp, sht4.humidity);
            LoRaSendFrame(0x0000, buffer, len);
        }

        delay(1000 + random(1000, 2000));

        if (bmp.update()) {
            Serial.println("-----BMP280-----");
            Serial.print(F("Temperature: "));
            -Serial.print(bmp.cTemp);
            Serial.println(" degrees C");
            Serial.print(F("Pressure: "));
            Serial.print(bmp.pressure);
            Serial.println(" Pa");
            Serial.print(F("Approx altitude: "));
            Serial.print(bmp.altitude);
            Serial.println(" m");
            Serial.println("-------------\r\n");
            memset(buffer, 0, sizeof(buffer));
            int len = sprintf(buffer, "Addr:%04X, Pressure:%.2f", DEVICE_LOCAL_ADDR, bmp.pressure);
            LoRaSendFrame(0x0000, buffer, len);
        }
        delay(1000 + random(1000, 2000));

#endif
        // M5.update();
        // if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
        //     // ESP32がコンソールから読み込む
        //     if (lora.SendFrame(config, (uint8_t *)msg, strlen(msg)) == 0) {
        //         print_log("send succeeded.");
        //         print_log("");
        //     } else {
        //         print_log("send failed.");
        //         print_log("");
        //     }      // }4f
        // delay(1);
    }
}

void LoRaRecvTask(void *pvParameters) {
    while (1) {
        if (lora.RecieveFrame(&data) == 0) {
            print_log("recv data:");
            for (int i = 0; i < data.recv_data_len; i++) {
                Serial.printf("%c", data.recv_data[i]);
                canvas.printf("%c", data.recv_data[i]);
            }
            print_log("");
            print_log("hex dump:");
            for (int i = 0; i < data.recv_data_len; i++) {
                Serial.printf("%02x ", data.recv_data[i]);
                canvas.printf("%02x ", data.recv_data[i]);
            }
            Serial.printf("\n");
            Serial.printf("RSSI: %d dBm\n", data.rssi);
            canvas.printf("RSSI: %d dBm\n", data.rssi);
            Serial.printf("\n");
            Serial.flush();
            canvas.pushSprite(13, 148);
        }

        delay(1);
    }
}

void LoRaSendTask(void *pvParameters) {
    while (1) {
        char msg[200] = {0};
        // ESP32がコンソールから読み込む
        ReadDataFromConsole(msg, (sizeof(msg) / sizeof(msg[0])));
        if (lora.SendFrame(config, (uint8_t *)msg, strlen(msg)) == 0) {
            print_log("send succeeded.");
            print_log("");
        } else {
            print_log("send failed.");
            print_log("");
        }

        Serial.flush();

        delay(1);
    }
}

void ReadDataFromConsole(char *msg, int max_msg_len) {
    int len       = 0;
    char *start_p = msg;

    while (len < max_msg_len) {
        if (Serial.available() > 0) {
            char incoming_byte = Serial.read();
            if (incoming_byte == 0x00 || incoming_byte > 0x7F) continue;
            *(start_p + len) = incoming_byte;
            // 最短で3文字(1文字 + CR LF)
            if (incoming_byte == 0x0a && len >= 2 && (*(start_p + len - 1)) == 0x0d) {
                break;
            }
            len++;
        }
        delay(1);
    }

    // msgからCR LFを削除
    len = strlen(msg);
    for (int i = 0; i < len; i++) {
        if (msg[i] == 0x0D || msg[i] == 0x0A) {
            msg[i] = '\0';
        }
    }
}

bool LoRaSendFrame(uint16_t addr, char *buffer, int len) {
    config.target_address = addr;
    if (lora.SendFrame(config, (uint8_t *)buffer, len) == 0) {
        print_log("send succeeded.");
        print_log(buffer);
        return true;
    } else {
        print_log("send failed.");
        print_log("");
        return false;
    }
}

void initSensor() {
#if DEVICE_LOCAL_ADDR == 0x0001

    if (!scd4x.begin(&Wire, SCD4X_I2C_ADDR, 21, 22, 400000U)) {
        Serial.println("Couldn't find SCD4X");
        while (1) delay(1);
    }
    uint16_t error;
    // stop potentially previously started measurement
    error = scd4x.stopPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    }

    // Start Measurement
    error = scd4x.startPeriodicMeasurement();
    if (error) {
        Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    }

    Serial.println("Waiting for first measurement... (5 sec)");
#endif

#if DEVICE_LOCAL_ADDR == 0x0002
    if (!sht4.begin(&Wire, SHT40_I2C_ADDR_44, 21, 22, 400000U)) {
        Serial.println("Couldn't find SHT4x");
        while (1) delay(1);
    }

    // You can have 3 different precisions, higher precision takes longer
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);

    if (!bmp.begin(&Wire, BMP280_I2C_ADDR, 21, 22, 400000U)) {
        Serial.println("Couldn't find BMP280");
        while (1) delay(1);
    }
    /* Default settings from datasheet. */
    bmp.setSampling(BMP280::MODE_NORMAL,     /* Operating Mode. */
                    BMP280::SAMPLING_X2,     /* Temp. oversampling */
                    BMP280::SAMPLING_X16,    /* Pressure oversampling */
                    BMP280::FILTER_X16,      /* Filtering. */
                    BMP280::STANDBY_MS_500); /* Standby time. */

#endif
}

void print_log(String info) {
    canvas.println(info);
    canvas.pushSprite(13, 148);
    Serial.println(info);
}