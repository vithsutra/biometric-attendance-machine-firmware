#include <Arduino.h>
#include <Preferences.h>
#include<RTClib.h>
#include<WiFi.h>
#include<Vsense_Finger_Print.h>
#include<BluetoothSerial.h>
#include <ArduinoJson.h>
#include<PubSubClient.h>
#include<string.h>
#include<stdlib.h>
#include<arduino_base64.hpp>

//please update these fields porperly before flashing the code to controller
const char unitId[] PROGMEM = "vs242st1";
const char userId[] PROGMEM = "6256deb4-8643-4ec3-8ba9-d7f4b98fcd01";
const char defaultSsid[] PROGMEM = "rust";
const char defaultPassword[] PROGMEM = "password";

const char mqttHost[] PROGMEM = "biometric.mqtt.vsensetech.in";
const char mqttUserName[] PROGMEM  = "biometric_unit";
const char mqttPassword[] PROGMEM = "Vithsutra@biometric@2025";
const char mqttPublishTopic[] PROGMEM = "biometric/message/processor";
#define MQTT_PORT 1883
#define MQTT_KEEP_ALIVE_INTERVAL 5 // in seconds


#define DELAY_PER_ITERATION 100 //in miliseconds
#define FINGERPRINT_SENSOR_COMM_BD_RATE 57600
#define WIFI_CONNECTION_TIMEOUT 5 //in seconds
#define MQTT_CONNECTION_TIMEOUT 5 // in seconds

const char configFilePath[] PROGMEM = "/config.json";

//digital pins 
#define NET_LED_PIN 26
#define STAT_LED_PIN 27
#define FAULT_LED_PIN 14
#define BUZZER_PIN 4

String bluetoothMessage = "";

//instance creation
Preferences store;
RTC_DS3231 rtc;
HardwareSerial serialPort(2);
Adafruit_Fingerprint finger=Adafruit_Fingerprint(&serialPort); 
WiFiClient tcp;
PubSubClient mqtt(tcp);
BluetoothSerial bluetooth;
DynamicJsonDocument json(1024);

//device runtime states
uint8_t wifiConnectionIndicated = 0;
uint8_t wifiDisconnectionIndicated = 0;
uint8_t mqttConnectionIndicated = 0;
uint8_t mqttDisconnectionIndicated = 0;
uint8_t mqttMessagePublished = 0;
uint8_t mqttConnectionStatusUpdatedToServer=0;
uint32_t mqttMessagePublishTimeOutCounter = MQTT_CONNECTION_TIMEOUT * 10;
uint8_t deleteSyncCompleted=0;
uint8_t insertSyncCompleted=0;
uint8_t bluetoothConnectionIndicated = 0;



//device indicaters
void turnOffAllIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,0);
  digitalWrite(BUZZER_PIN,0);
}

void powerOnIndicater() {
  digitalWrite(STAT_LED_PIN,1);
}

void controllerFaultIndicater() {
  turnOffAllIndicater();
  digitalWrite(FAULT_LED_PIN,1);
  while(1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void wifiConnectionIndicater() {
  digitalWrite(NET_LED_PIN,1);
  digitalWrite(STAT_LED_PIN,1);
  digitalWrite(FAULT_LED_PIN,0);
}

void wifiDisconnectionIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,1);
  digitalWrite(FAULT_LED_PIN,0);
}

void mqttConnectionIndicater() {
  digitalWrite(NET_LED_PIN,1);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,0);
}

void mqttDisconnectionIndicater() {
  digitalWrite(NET_LED_PIN,1);
  digitalWrite(STAT_LED_PIN,1);
  digitalWrite(FAULT_LED_PIN,0);
}

void bluetoothConnectionIndicater() {
  digitalWrite(STAT_LED_PIN,1);
  digitalWrite(NET_LED_PIN,1);
  digitalWrite(FAULT_LED_PIN,1);
}

void serverFaultIndicater() {
  turnOffAllIndicater();
  uint8_t serverFaultTimeOutCounter = 5; //in secs
  while(serverFaultTimeOutCounter) {
    digitalWrite(FAULT_LED_PIN,1);
    vTaskDelay(pdMS_TO_TICKS(1000));
    digitalWrite(FAULT_LED_PIN,0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    serverFaultTimeOutCounter--;
  }
}


void fingerPrintSensorErrorIndicater() {
  digitalWrite(BUZZER_PIN,1);
  delay(200);
  digitalWrite(BUZZER_PIN,0);
  delay(15);
  digitalWrite(BUZZER_PIN,1);
  delay(200);
  digitalWrite(BUZZER_PIN,0);
  delay(20);
  digitalWrite(BUZZER_PIN,1);
  delay(200);
  digitalWrite(BUZZER_PIN,0);
  delay(15);
}

void attendanceSuccessIndicater() {
  digitalWrite(BUZZER_PIN,1);
  digitalWrite(STAT_LED_PIN,1);
  delay(1000);
  digitalWrite(BUZZER_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
}

void printFreeMemory() {
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.print(" KB");
  Serial.print(" | Min Heap: ");
  Serial.print(ESP.getMinFreeHeap() / 1024); 
  Serial.println(" KB");
}

void updateMqttConnectionStatusToServer() {
  json.clear();
  json["mty"]="a";
  json["uid"]=unitId;
  char jsonMessage[200];
  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(mqttPublishTopic,jsonMessage);
}

void syncDeletes() {
  json.clear();
  json["mty"]="c";
  json["uid"]=unitId;
  char jsonMessage[200];
  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(mqttPublishTopic,jsonMessage);
}

void updateStudentDeleteStatusToServer(const char *studentId) {
  json.clear();
  json["mty"]="d";
  json["uid"]=unitId;
  json["sid"]=studentId;

  char jsonMessage[200];

  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(mqttPublishTopic,jsonMessage);
}

void syncInserts() {
  json.clear();
  json["mty"]="e";
  json["uid"]=unitId;

  char jsonMessage[200];
  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(mqttPublishTopic,jsonMessage);
}

void updateStudentInsertStatusToServer(const char *studentId) {
  json.clear();
  json["mty"]="f";
  json["uid"]=unitId;
  json["sid"]=studentId;

  char jsonMessage[200];

  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(mqttPublishTopic,jsonMessage);
}

void updateAttendanceToServer(uint16_t studentId) {
  json.clear();
  DateTime now = rtc.now();
  String timeStamp = now.timestamp();

  json["mty"] = "g";
  json["uid"] = unitId;
  json["sid"] = String(studentId);
  json["tm"] = timeStamp;

  char jsonMessage[200];
  serializeJson(json,jsonMessage);

  mqtt.publish(mqttPublishTopic,jsonMessage);
}

uint8_t deleteStudentFingerPrint(uint16_t studentId) {
  if(finger.deleteModel(studentId) == FINGERPRINT_OK) {
    return 1;
  }
  return 0;
}

uint8_t insertStudentFingerPrint(uint16_t studentId,const char *fingerPrintData) {
  unsigned char *buffer = new unsigned char[512];

  base64Decoder(fingerPrintData,buffer);

  if(finger.write_template_to_sensor(512,buffer)) {
    if(finger.storeModel(studentId) == FINGERPRINT_OK) {
      return 1;
    }
  }
  return 0;
}

void base64Decoder(const char *input, unsigned char *output) {
  base64::decode(input,output);
}

uint8_t takeFingerPrint() {
    int sensorStatus = finger.getImage();
    if(sensorStatus == FINGERPRINT_NOFINGER) {
      return 0;
    }

    if(sensorStatus == FINGERPRINT_PACKETRECIEVEERR) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus == FINGERPRINT_IMAGEFAIL) {
      fingerPrintSensorErrorIndicater();
      return 0;     
    }

    sensorStatus = finger.image2Tz();

    if(sensorStatus == FINGERPRINT_IMAGEMESS) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus == FINGERPRINT_PACKETRECIEVEERR) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus == FINGERPRINT_FEATUREFAIL) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus ==  FINGERPRINT_INVALIDIMAGE) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    sensorStatus = finger.fingerSearch();

    if(sensorStatus == FINGERPRINT_PACKETRECIEVEERR) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus == FINGERPRINT_NOTFOUND) {
      fingerPrintSensorErrorIndicater();
      return 0;
    }

    if(sensorStatus == FINGERPRINT_OK) {
      return 1;
    }

    fingerPrintSensorErrorIndicater();
    return 0;
}

void mqttMessageHandler(char *topic, byte *payload,unsigned int length) {
  char jsonBuffer[1024];
  memcpy(jsonBuffer,payload,length);
  jsonBuffer[length]='\0';

  json.clear();

  DeserializationError err = deserializeJson(json,jsonBuffer);

  if(err) {
    // Serial.println(F("error occurred while decoding json"));
    WiFi.disconnect();
    serverFaultIndicater();
    return;
  }

  const char *errorStatus = json["est"];

  if(strcmp(errorStatus,"1") == 0) {
    // Serial.println(F("error occurred while decoding json"));
    WiFi.disconnect();
    serverFaultIndicater();
    return;
  }
  
  const char *messageType = json["mty"];


  if(strcmp(messageType,"1") == 0) {
    mqttConnectionStatusUpdatedToServer=1;
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    return;
  }

  if(strcmp(messageType,"2") == 0) {

    const char *studentsEmptyStatus = json["ste"];

    if(strcmp(studentsEmptyStatus,"1") == 0) {
      deleteSyncCompleted=1;
      mqttMessagePublished=0;
      mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
      return;
    }

      const char *studentId = json["sid"];
      int studentIdInt = atoi(studentId);

      if(studentIdInt < 1) {
          // Serial.println(F("error occurred student id cannot be lessthan 1"));
          WiFi.disconnect();
          serverFaultIndicater();
          return;
      }

      if(deleteStudentFingerPrint((uint16_t)studentIdInt)) {
        updateStudentDeleteStatusToServer(studentId);
      }
      return;
  }

  if(strcmp(messageType,"3") == 0) {
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    return;
  }

  if(strcmp(messageType,"4") == 0) {
    const char *studentEmptyStatus = json["ste"];
    if(strcmp(studentEmptyStatus,"1") == 0) {
      insertSyncCompleted=1;
      mqttMessagePublished=0;
      mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
      return;
    }

    const char *studentId = json["sid"];
    int studentIdInt = atoi(studentId);

    if(studentIdInt < 1) {
      // Serial.println(F("error occurred student id cannot be lessthan 1"));
      WiFi.disconnect();
      serverFaultIndicater();
      return;
    }

    const char *fingerPrintData = json["fpd"];

    if(insertStudentFingerPrint((uint16_t)studentIdInt,fingerPrintData)) {
      updateStudentInsertStatusToServer(studentId);
    }
    return;
  }

  if(strcmp(messageType,"5") == 0) {
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    return;
  }

  if(strcmp(messageType,"6") == 0) {
    attendanceSuccessIndicater();
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    return;
  }

  return;
}

void sendBluetoothResponse(const char *message) {
  for(uint8_t i=0; message[i];i++) {
    bluetooth.write(message[i]);
  }
}

void configWifi() {
  json.clear();
  DeserializationError err=deserializeJson(json,bluetoothMessage.c_str());
  if(err) {
    const char *jsonResponse = "{\"est\":\"1\",\"ety\":\"2\"}";
    sendBluetoothResponse(jsonResponse);
    bluetooth.disconnect();
    bluetoothMessage.clear();
    bluetoothConnectionIndicated=0;
    return;
  }

  String inputUserId = json["user_id"];

  if(strcmp(inputUserId.c_str(),userId) != 0) {
    const char * jsonResponse = "{\"est\":\"1\",\"ety\":\"4\"}";
    sendBluetoothResponse(jsonResponse);
    bluetooth.disconnect();
    bluetoothMessage.clear();
    bluetoothConnectionIndicated=0;
    return;
  }

  store.begin("config",false);

  String ssid = json["ssid"];
  String pwd = json["password"];

  store.putString("ssid",ssid);
  store.putString("pwd",pwd);

  store.end();
  const char * jsonResponse = "{\"est\":\"0\"}";
  sendBluetoothResponse(jsonResponse);
  bluetooth.disconnect();
  bluetoothMessage.clear();
  bluetoothConnectionIndicated=0;
  return;

}

//wifi connection
void connectToWifi() {
  uint8_t timeOutCounter = WIFI_CONNECTION_TIMEOUT;
  store.begin("config",true);
  String ssid = store.getString("ssid");
  String password = store.getString("pwd");
  store.end();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(),password.c_str());

  while(WiFi.status() != WL_CONNECTED && timeOutCounter > 0 ) {
    if(bluetooth.connected()) {
      return;
    }
    // Serial.println(F("connecting to wifi"));
    timeOutCounter--;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void connectToBroker() {
  uint8_t timeOutCounter = MQTT_CONNECTION_TIMEOUT;
  
  mqtt.setServer(mqttHost,MQTT_PORT);
  mqtt.setCallback(mqttMessageHandler);
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE_INTERVAL);

  json.clear();
  json["mty"]="b";
  json["uid"]=unitId;

  char lastWillJsonMessage[200];

  serializeJson(json,lastWillJsonMessage,sizeof(lastWillJsonMessage));

  while( WiFi.status() == WL_CONNECTED && !mqtt.connected() && timeOutCounter > 0) {
    if(bluetooth.connected()) {
      return;
    }
    mqtt.connect(unitId,mqttUserName,mqttPassword,mqttPublishTopic,1,false,lastWillJsonMessage,false);
    // Serial.println(F("connecting to broker"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    timeOutCounter--;
  }

  if(mqtt.connected()) {
    mqtt.subscribe(unitId);
  }

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  //digital pins setup
  pinMode(NET_LED_PIN,OUTPUT);
  pinMode(STAT_LED_PIN,OUTPUT);
  pinMode(FAULT_LED_PIN,OUTPUT);
  pinMode(BUZZER_PIN,OUTPUT);

  turnOffAllIndicater();
  powerOnIndicater();

  bluetooth.begin(unitId);

  if(!rtc.begin()) {
    // Serial.println(F("rtc module not found"));
    controllerFaultIndicater();
  }

  finger.begin(FINGERPRINT_SENSOR_COMM_BD_RATE);

  if(!finger.verifyPassword()) {
    // Serial.println(F("finger print sensor not found"));
    controllerFaultIndicater();
  }

  store.begin("config",false);

  //setting the mode if key not exists
  if(!store.isKey("mode")) {
    store.putUShort("mode",1);
  }

  uint16_t mode = store.getUShort("mode",0);

  if(mode){
    //setting rtc time
    // Serial.println(F("setting the initial config settings"));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    store.putString("ssid",defaultSsid);
    store.putString("pwd",defaultPassword);
    store.putUShort("mode",0);
  }

  store.end();

  // bluetooth.begin(unitId);

  connectToWifi();

  connectToBroker();

}

void loop() {

  while(bluetooth.connected()) {
    if(!bluetoothConnectionIndicated) {
      WiFi.disconnect(true);
      bluetoothConnectionIndicater();
      bluetoothConnectionIndicated=1;
    }

    if(bluetooth.available()) {
      char incomingChar = bluetooth.read();
      if(incomingChar != '*') {
        bluetoothMessage+=incomingChar;
      } else {
        configWifi();
      }
    }
   }
 

  if(WiFi.status() == WL_CONNECTED && !wifiConnectionIndicated) {
    // Serial.println(F("wifi connected"));
    wifiConnectionIndicater();
    wifiConnectionIndicated=1;
    wifiDisconnectionIndicated=0;
  }

  if(WiFi.status() != WL_CONNECTED && !wifiDisconnectionIndicated){
    // Serial.println(F("wifi disconnected"));
    wifiDisconnectionIndicater();
    wifiDisconnectionIndicated=1;
    wifiConnectionIndicated=0;
  }

  if(WiFi.status() != WL_CONNECTED) {
    //resetting the states
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    mqttConnectionStatusUpdatedToServer=0;
    deleteSyncCompleted=0;
    insertSyncCompleted=0;
    bluetoothConnectionIndicated=0;
    mqtt.disconnect();
    connectToWifi();
  }

  if(mqtt.connected() && !mqttConnectionIndicated) {
    // Serial.println(F("broker connected"));
    mqttConnectionIndicater();
    mqttConnectionIndicated=1;
    mqttDisconnectionIndicated=0;
  }

  if(WiFi.status() == WL_CONNECTED && !mqtt.connected() && !mqttDisconnectionIndicated) {
    // Serial.println(F("broker disconnected"));
    mqttDisconnectionIndicater();
    mqttDisconnectionIndicated=1;
    mqttConnectionIndicated=0;
  }


  if(!mqtt.connected()) {
    //resetting the states
    mqttMessagePublished=0;
    mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
    mqttConnectionStatusUpdatedToServer=0;
    deleteSyncCompleted=0;
    insertSyncCompleted=0;
    bluetoothConnectionIndicated=0;
    connectToBroker();
  }

  if(!mqttConnectionStatusUpdatedToServer && !mqttMessagePublished) {
    updateMqttConnectionStatusToServer();
    mqttMessagePublished=1;
  }

  if(mqttConnectionStatusUpdatedToServer && !deleteSyncCompleted && !mqttMessagePublished) {
    syncDeletes();
    mqttMessagePublished=1;
  }

  if(deleteSyncCompleted && !insertSyncCompleted && !mqttMessagePublished) {
    syncInserts();
    mqttMessagePublished=1;
  }

  if(insertSyncCompleted && !mqttMessagePublished) {
    uint8_t sensorStatus = takeFingerPrint();
    if(sensorStatus) {
      updateAttendanceToServer(finger.fingerID);
      mqttMessagePublished=1;
    }
  }
 
   if(mqttMessagePublished) {
      mqttMessagePublishTimeOutCounter--;
   }

   if(mqttMessagePublishTimeOutCounter == 0) {
      mqttMessagePublished=0;
      mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
   }

  mqtt.loop();
  printFreeMemory();
  vTaskDelay(pdMS_TO_TICKS(DELAY_PER_ITERATION));
}
