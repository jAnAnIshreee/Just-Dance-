#include <ESP8266WiFi.h>
#include <espnow.h>
#include <string.h>

// espB MAC (Controller 1)
uint8_t espBAddress[] = {0x48, 0x3F, 0xDA, 0x56, 0x73, 0xCF};
// espA MAC (Controller 2)
uint8_t espAAddress[] = {0x84, 0xF3, 0xEB, 0xD9, 0x82, 0x76};

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
      // Check the sender ID to format the string for Python!
      if (rxData.senderID == 'B') {
        Serial.print("S1:");
        Serial.println(rxData.score);
      } else if (rxData.senderID == 'A') {
        Serial.print("S2:");
        Serial.println(rxData.score);
      } else {
        // Fallback just in case
        Serial.println(rxData.score);
      }
      
      digitalWrite(LED_BUILTIN, LOW);
      delay(20);
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50); // Important: Prevents delay when reading string commands
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != 0) return;

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Register BOTH peers
  esp_now_add_peer(espBAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  esp_now_add_peer(espAAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read(); // Read exactly one character

    if (cmd == '0' || cmd == '1') {
      // Send song selection to BOTH controllers
      txData.controlValue = (cmd == '0') ? 0 : 1;
      esp_now_send(espBAddress, (uint8_t *) &txData, sizeof(txData));
      esp_now_send(espAAddress, (uint8_t *) &txData, sizeof(txData));
    } 
    else if (cmd == 'B') {
      // Send Start command (2) ONLY to ESP B (Player 1)
      txData.controlValue = 2; 
      esp_now_send(espBAddress, (uint8_t *) &txData, sizeof(txData));
    } 
    else if (cmd == 'R') {
      // Send Start command (2) ONLY to ESP A (Player 2)
      txData.controlValue = 2; 
      esp_now_send(espAAddress, (uint8_t *) &txData, sizeof(txData));
    }
  }
}