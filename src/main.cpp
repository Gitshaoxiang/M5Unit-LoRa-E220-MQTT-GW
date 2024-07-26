#include <M5Unified.h>
#include <M5GFX.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "M5_LoRa_E220.h"

#include "lora_gw_bg.h"
#include "lora_node_bg.h"

QueueHandle_t QueueHandle;
const int QueueElementSize = 10;

typedef struct {
    uint8_t data[200];
    uint8_t len;
    uint16_t addr;
} message_t;

#define DEVICE_LOCAL_ADDR 0x0000
#define MQTT_UPLLINK      "E220/up"
#define MQTT_DOWNLINK     "E220/down/#"

WiFiClient espClient;
PubSubClient client(espClient);

// Configure the name and password of the connected wifi and your MQTT Serve
const char *ssid        = "xxxxxxxxxxxxxxx";
const char *password    = "xxxxxxxxxxxxxxx";
const char *mqtt_server = "broker.emqx.io";

LoRa_E220 lora;
struct LoRaConfigItem_t config;
struct RecvFrame_t data;

/** prototype declaration **/
void LoRaRecvTask(void *pvParameters);
void LoRaSendTask(void *pvParameters);
void LoRaBtnTask(void *pvParameters);
void ReadDataFromConsole(char *msg, int max_msg_len);

void networkInit();
void reConnect();
void callback(char *topic, byte *payload, unsigned int length);

M5Canvas canvas(&M5.Display);

void print_log(String info) {
    canvas.println(info);
    canvas.pushSprite(13, 148);
    Serial.println(info);
}

void gfx_canvas_init() {
    M5.Display.setFont(&fonts::Orbitron_Light_32);
    M5.Display.setTextDatum(top_center);
    M5.Display.setTextColor(WHITE);
    M5.Display.pushImage(0, 0, 320, 240, image_data_lora_gw_bg);
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
                  // if use port a

    // Create the queue which will have <QueueElementSize> number of elements, each of size `message_t` and pass the
    // address to <QueueHandle>.
    QueueHandle = xQueueCreate(QueueElementSize, sizeof(message_t));

    lora.Init(&Serial2, 9600, SERIAL_8N1, 33, 32);
    // lora.Init();

    networkInit();

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
    // xTaskCreateUniversal(LoRaBtnTask, "LoRaBtnTask", 8192, NULL, 1, NULL, APP_CPU_NUM);
}

void loop() {
    // put your main code here, to run repeatedly:
    if (!client.connected()) {
        reConnect();
    }
    client.loop();  // This function is called periodically to allow clients to
                    // process incoming messages and maintain connections to the
                    // server.
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
            char payload[data.recv_data_len + 1];
            memcpy(payload, data.recv_data, data.recv_data_len);
            payload[data.recv_data_len] = '\0';
            client.publish(MQTT_UPLLINK, payload, data.recv_data_len);
        }

        delay(1);
    }
}

void LoRaSendTask(void *pvParameters) {
    message_t msg;

    while (1) {
        // char msg[200] = {0};
        // ESP32がコンソールから読み込む
        // ReadDataFromConsole(msg, (sizeof(msg) / sizeof(msg[0])));

        int ret = xQueueReceive(QueueHandle, &msg, portMAX_DELAY);
        if (ret == pdPASS) {
            // The message was successfully received - send it back to Serial port and "Echo: "
            config.target_address = msg.addr;
            if (lora.SendFrame(config, (uint8_t *)msg.data, msg.len) == 0) {
                print_log("send succeeded.");
                print_log("");
            } else {
                print_log("send failed.");
                print_log("");
            }

            Serial.flush();

            // The item is queued by copy, not by reference, so lets free the buffer after use.
        } else if (ret == pdFALSE) {
            Serial.println("The `TaskWriteToSerial` was unable to receive data from the Queue");
        }

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

void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    char buffer[length + 1] = {0};
    memcpy(buffer, payload, length);
    buffer[length] = '\0';

    message_t message;
    String _topic   = String(topic);
    String addr_str = _topic.substring(10);

    print_log(addr_str);
    print_log(buffer);

    message.addr = addr_str.toInt();
    message.len  = length > 200 ? 200 : length;
    memcpy(message.data, payload, message.len);
    int ret = xQueueSend(QueueHandle, (void *)&message, 0);
}

void networkInit() {
    WiFi.mode(WIFI_STA);         // Set the mode to WiFi station mode.  设置模式为WIFI站模式
    WiFi.begin(ssid, password);  // Start Wifi connection.  开始wifi连接
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    client.setServer(mqtt_server,
                     1883);        // Sets the server details.  配置所连接的服务器
    client.setCallback(callback);  // Sets the message callback function.  设置消息回调函数
}

void reConnect() {
    while (!client.connected()) {
        Serial.println("Attempting MQTT connection...");
        // Create a random client ID.  创建一个随机的客户端ID
        String clientId = "M5Stack-";
        clientId += String(random(0xffff), HEX);
        // Attempt to connect.  尝试重新连接
        if (client.connect(clientId.c_str())) {
            Serial.println("\nSuccess\n");
            // Once connected, publish an announcement to the topic.
            // 一旦连接，发送一条消息至指定话题
            client.publish(MQTT_UPLLINK, "Gateway Online");
            // ... and resubscribe.  重新订阅话题
            client.subscribe(MQTT_DOWNLINK);
            print_log("Gateway Online");

        } else {
            Serial.println(client.state());
            delay(5000);
        }
    }
}
