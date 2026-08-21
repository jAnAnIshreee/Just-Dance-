#include <ESP8266WiFi.h>
#include <espnow.h>
#include <string.h>

// espB MAC
uint8_t espBAddress[] = {0x48, 0x3F, 0xDA, 0x56, 0x73, 0xCF};

#define FEATURE_COUNT 32
#define MOVE_NAME_LEN 24

typedef struct struct_message_tm4c_event {
  char senderID;
  char packetType;
  char moveName[MOVE_NAME_LEN];
  int32_t score;
  int32_t features[FEATURE_COUNT];
  uint8_t featureCount;
  uint32_t sendCount;
} struct_message_tm4c_event;

typedef struct struct_message_central_to_node {
  int controlValue;
} struct_message_central_to_node;

struct_message_tm4c_event rxData;
struct_message_central_to_node txData;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(struct_message_tm4c_event)) {
    memcpy(&rxData, incomingData, sizeof(rxData));
    
    if (rxData.packetType == 'D') {
      Serial.println(rxData.score);
      
      digitalWrite(LED_BUILTIN, LOW);
      delay(20);
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != 0) return;

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_add_peer(espBAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'G' || c == 'P' || c == 'S') {
      digitalWrite(LED_BUILTIN, LOW);
      
      if (c == 'G') txData.controlValue = 0;
      else if (c == 'P') txData.controlValue = 1;
      else if (c == 'S') txData.controlValue = 2; // Start Command!
      
      esp_now_send(espBAddress, (uint8_t *)&txData, sizeof(txData));
      
      delay(50);
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}