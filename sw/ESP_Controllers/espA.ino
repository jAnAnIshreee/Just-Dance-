#include <ESP8266WiFi.h>
#include <espnow.h>
#include <string.h>
#include <stdlib.h>

// Hub (ESP D) MAC ADDRESS
//uint8_t espDAddress[] = {0x98, 0xF4, 0xAB, 0xBE, 0xE8, 0x91};
uint8_t espCAddress[] = {0xDC, 0x4F, 0x22, 0x6C, 0xC5, 0x45};

#define FEATURE_COUNT 32
#define MOVE_NAME_LEN 24
#define LINE_BUF_LEN 512

uint32_t sendCount = 0;
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

struct_message_tm4c_event txData;
struct_message_central_to_node rxData;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  if (len == sizeof(struct_message_central_to_node)) {
    memcpy(&rxData, incomingData, sizeof(rxData));
    
    digitalWrite(LED_BUILTIN, LOW); // Flash LED so you know it got a command

    if (rxData.controlValue == 0) {
      Serial.println('0');
    } else if (rxData.controlValue == 1) {
      Serial.println('1');
    } else if (rxData.controlValue == 2) {
      // Output 'V' without a newline! 
      // This is exactly what TM4C's Wait_For_Char('V') needs to escape the loop.
      Serial.print('S'); 
    }

    delay(50);
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

void initPacket(struct_message_tm4c_event *p) {
  memset(p, 0, sizeof(*p));
  p->senderID = 'A'; // Labeling as Controller 2 / ESP A
  p->sendCount = sendCount;
}

// Parses: DETECT,MoveName,1234
bool parseDetectLine(char *line, struct_message_tm4c_event *out) {
  char *saveptr = NULL;
  char *token;
  token = strtok_r(line, ",", &saveptr);
  if (token == NULL || strcmp(token, "DETECT") != 0) return false;
  token = strtok_r(NULL, ",", &saveptr);
  if (token == NULL) return false;

  initPacket(out);
  out->packetType = 'D';
  strncpy(out->moveName, token, MOVE_NAME_LEN - 1);
  out->moveName[MOVE_NAME_LEN - 1] = '\0';
  
  token = strtok_r(NULL, ",", &saveptr);
  if (token == NULL) return false;
  out->score = strtol(token, NULL, 10);

  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn LED OFF

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != 0) return;

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_add_peer(espCAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

void loop() {
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      char lineBuf[LINE_BUF_LEN];
      msg.toCharArray(lineBuf, sizeof(lineBuf));
      if (parseDetectLine(lineBuf, &txData)) {
        // Flash LED on score transmission!
        digitalWrite(LED_BUILTIN, LOW);
        if (esp_now_send(espCAddress, (uint8_t *)&txData, sizeof(txData)) == 0) sendCount++;
        delay(20);
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
  }
}