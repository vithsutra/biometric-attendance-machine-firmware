#include <Arduino.h>
#include <Preferences.h>
#include <RTClib.h>
#include <WiFi.h>
#include <Vithsutra_FingerPrint.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <string.h>
#include <stdlib.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <arduino_base64.hpp>

//please update these fields porperly before flashing the code to controller
#define UNIT_ID "vs242s000"
#define USER_ID "6256deb4-8643-4ec3-8ba9-d7f4b98fcd01"
#define DEFAULT_SSID ".rs"
#define DEFAULT_PASSWORD "password"

#define MQTT_BROKER_HOST "biometric.mqtt.broker1.vithsutra.com"
#define MQTT_BROKER_USERNAME "biometric_unit"
#define MQTT_BROKER_PASSWORD "Vithsutra@biometric@2025"
#define MQTT_BROKER_PORT 1883
#define MQTT_KEEP_ALIVE_INTERVAL 5 // in seconds
#define MQTT_CONNECTION_UPDATE_TOPIC UNIT_ID "/connection"
#define MQTT_DISCONNECTION_TOPIC UNIT_ID "/disconnection"
#define MQTT_DELETESYNC_TOPIC UNIT_ID "/deletesync"
#define MQTT_DELETESYNC_ACK_TOPIC UNIT_ID "/deletesyncack"
#define MQTT_INSERTSYNC_TOPIC UNIT_ID "/insertsync"
#define MQTT_INSERTSYNC_ACK_TOPIC UNIT_ID "/insertsyncack"
#define MQTT_ATTENDANCE_TOPIC UNIT_ID "/attendance"

//BLE
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLUETOOTH_MAX_ALIVE_TIME 60 //in secs
#define BLUETOOTH_COUNT_INTERVAL 1000 // in milli secs
 
#define DELAY_PER_ITERATION 100 //in miliseconds
#define FINGERPRINT_SENSOR_COMM_BD_RATE 57600
#define MQTT_CONNECTION_TIMEOUT 5 // in seconds

//digital pins 
#define NET_LED_PIN 26
#define STAT_LED_PIN 27
#define FAULT_LED_PIN 14
#define BUZZER_PIN 4

//instance creation
Preferences store;
RTC_DS3231 rtc;
HardwareSerial serialPort(2);
Adafruit_Fingerprint finger=Adafruit_Fingerprint(&serialPort); 
WiFiClient tcp;
PubSubClient mqtt(tcp);
DynamicJsonDocument json(1024);
//BLE
BLEServer *pServer;
BLEService *pService;
BLECharacteristic *pCharacteristic;



unsigned long prevMillis = 0; 

//BLE states
uint8_t bluetoothConnected = 0;
uint8_t bluetoothAliveCounter = 0;
uint8_t wifiConfigured=0;
uint8_t bleDisabled = 0;

//device runtime states
uint8_t mqttConnectionIndicated = 0;
uint8_t mqttDisconnectionIndicated = 0;
uint8_t mqttMessagePublished = 0;
uint8_t mqttConnectionStatusUpdatedToServer=0;
uint32_t mqttMessagePublishTimeOutCounter = MQTT_CONNECTION_TIMEOUT * 10;
uint8_t deleteSyncCompleted=0;
uint8_t insertSyncCompleted=0;

//device indicaters
void turnOffAllIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,0);
  digitalWrite(BUZZER_PIN,0);
}

void powerOnIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,1);
  digitalWrite(FAULT_LED_PIN,0);
}

//indicating the wifi config error inifinitely
void controllerFaultIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,0);

  while(1) {
    digitalWrite(BUZZER_PIN,1);
    digitalWrite(FAULT_LED_PIN,1);
    delay(1000);
    digitalWrite(BUZZER_PIN,0);
    digitalWrite(FAULT_LED_PIN,0);
    delay(1000);
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
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,1);
}

//indicating the wifi config error for 10 sec
void wifiConfigErrorIndicater() {
  digitalWrite(NET_LED_PIN,0);
  digitalWrite(STAT_LED_PIN,0);
  digitalWrite(FAULT_LED_PIN,0);
  uint8_t timeOutCounter = 10;
  while(timeOutCounter > 0 ){
    digitalWrite(FAULT_LED_PIN,1);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(FAULT_LED_PIN,0);
    vTaskDelay(pdMS_TO_TICKS(500));
    timeOutCounter--;
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

//BLE
class WiFiConfigBLEServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *server) {
      bluetoothConnected=1;
      mqtt.disconnect();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      bluetoothConnectionIndicater();
  }

  void onDisconnect(BLEServer *server) {
      ESP.restart();
  }
};

class WiFiCharacteristicServerCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    json.clear();
    DeserializationError err = deserializeJson(json,rxValue.c_str());
    if(err) {
      wifiConfigErrorIndicater();
      wifiConfigured=1;
      return;
    }
    String ssid = json["ssid"];
    String password = json["password"];

    store.begin("config",false);


    store.putString("ssid",ssid);
    store.putString("pwd",password);

    store.end();
    wifiConfigured=1;
  }
};

void printFreeMemory() {
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap() / 1024);
  Serial.print(" KB");
  Serial.print(" | Min Heap: ");
  Serial.print(ESP.getMinFreeHeap() / 1024); 
  Serial.println(" KB");
}

void updateMqttConnectionStatusToServer() {
  mqtt.publish(MQTT_CONNECTION_UPDATE_TOPIC,"");
}

void syncDeletes() {
  mqtt.publish(MQTT_DELETESYNC_TOPIC,"");
}

void updateStudentDeleteStatusToServer(const char *studentId) {
  json.clear();
  json["sid"]=studentId;

  char jsonMessage[200];

  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(MQTT_DELETESYNC_ACK_TOPIC,jsonMessage);
}

void syncInserts() {
  mqtt.publish(MQTT_INSERTSYNC_TOPIC,"");
}

void updateStudentInsertStatusToServer(const char *studentId) {
  json.clear();
  json["sid"]=studentId;

  char jsonMessage[200];

  serializeJson(json,jsonMessage,sizeof(jsonMessage));
  mqtt.publish(MQTT_INSERTSYNC_ACK_TOPIC,jsonMessage);
}

void updateAttendanceToServer(uint16_t studentId) {
  json.clear();
  DateTime now = rtc.now();
  String timeStamp = now.timestamp();

  json["sid"] = String(studentId);
  json["tmstmp"] = timeStamp;

  char jsonMessage[200];
  serializeJson(json,jsonMessage);

  mqtt.publish(MQTT_ATTENDANCE_TOPIC,jsonMessage);
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
  char jsonBuffer[2048];
  memcpy(jsonBuffer,payload,length);
  jsonBuffer[length]='\0';

  json.clear();

  DeserializationError err = deserializeJson(json,jsonBuffer);

  if(err) {
    mqtt.disconnect();
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
          mqtt.disconnect();
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
      mqtt.disconnect();
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

//BLE
void enableBLEStack() {
  BLEDevice::init(UNIT_ID);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new WiFiConfigBLEServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  pCharacteristic->setCallbacks(new WiFiCharacteristicServerCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}

void disableBLEStack() {
  BLEDevice::deinit(true);
  bluetoothConnected=0;
}

//cloud sync reset
void resetCloudSync() {
  mqttConnectionIndicated = 0;
  mqttDisconnectionIndicated = 0;
  mqttMessagePublished=0;
  mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
  mqttConnectionStatusUpdatedToServer=0;
  deleteSyncCompleted=0;
  insertSyncCompleted=0;
}

//wifi setup
void wifiEventHandler(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      //indicate the wifi connection
        wifiConnectionIndicater();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        mqtt.disconnect();
        wifiDisconnectionIndicater();
        resetCloudSync();
        WiFi.reconnect();
      //inidicate the wifi disconnection
  }
}

void connectToBroker() {
  uint8_t timeOutCounter = MQTT_CONNECTION_TIMEOUT;
  
  mqtt.setServer(MQTT_BROKER_HOST,MQTT_BROKER_PORT);
  mqtt.setCallback(mqttMessageHandler);
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE_INTERVAL);
  mqtt.setBufferSize(1024);

  while( WiFi.status() == WL_CONNECTED && !mqtt.connected() && timeOutCounter > 0) {
    if(bluetoothConnected) {
      return;
    }

    mqtt.connect(UNIT_ID,MQTT_BROKER_USERNAME,MQTT_BROKER_PASSWORD,MQTT_DISCONNECTION_TOPIC,1,false,"",false);
    // Serial.println("connecting to broker -> "+String(timeOutCounter));
    vTaskDelay(pdMS_TO_TICKS(1000));
    timeOutCounter--;
  }

  if(mqtt.connected()) {
    mqtt.subscribe(UNIT_ID);
  }

}


void setup() {
  // // put your setup code here, to run once:
  // Serial.begin(9600);

  //digital pins setup
  pinMode(NET_LED_PIN,OUTPUT);
  pinMode(STAT_LED_PIN,OUTPUT);
  pinMode(FAULT_LED_PIN,OUTPUT);
  pinMode(BUZZER_PIN,OUTPUT);

  turnOffAllIndicater();
  powerOnIndicater();

  //BLE setup
  enableBLEStack();

  if(!rtc.begin()) {
    // Serial.println(F("rtc module not found"));
    controllerFaultIndicater();
  }

  serialPort.begin(57600, SERIAL_8N1, 16, 17); // important set the baudrate for the serial port
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
    store.putString("ssid",DEFAULT_SSID);
    store.putString("pwd",DEFAULT_PASSWORD);
    store.putUShort("mode",0);
  }

  //wifi setup
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(1000));

  WiFi.onEvent(wifiEventHandler);
  WiFi.mode(WIFI_STA);

  String ssid = store.getString("ssid");
  String password = store.getString("pwd");

  store.end();

  if(ssid != "" && password != "") {
    WiFi.begin(ssid.c_str(),password.c_str());
  } else {
    // Serial.println("falling back to default wifi credentials");
    WiFi.begin(DEFAULT_SSID,DEFAULT_PASSWORD);
  }

  //mqtt broker connection
  connectToBroker();

}

void loop() {

  unsigned long currentMillis = millis();

  if(currentMillis - prevMillis >= BLUETOOTH_COUNT_INTERVAL) {
    prevMillis = currentMillis;
    //increment the bluetooth alive counter only when bluetooth is not connected
    if(!bluetoothConnected && bluetoothAliveCounter <= 60) {
      bluetoothAliveCounter++;
    }
  }

  if(!bleDisabled && bluetoothAliveCounter > BLUETOOTH_MAX_ALIVE_TIME ) {
    //disable the complete BLE 
    disableBLEStack();
    bleDisabled=1;
  }

  if(wifiConfigured) {
    disableBLEStack();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
  }

  if(!bluetoothConnected && mqtt.connected() && !mqttConnectionIndicated) {
    // Serial.println("connected to broker");
    mqttConnectionIndicater();
    mqttConnectionIndicated=1;
    mqttDisconnectionIndicated=0;
  }

  if(!bluetoothConnected && WiFi.status() == WL_CONNECTED && !mqtt.connected() && !mqttDisconnectionIndicated) {
    // Serial.println("disconnected from broker");
    mqttDisconnectionIndicater();
    mqttDisconnectionIndicated=1;
    mqttConnectionIndicated=0;
  }

  if(!bluetoothConnected && !mqtt.connected()) {
    //resetting the states
    resetCloudSync();
    connectToBroker();
  }

  if(!bluetoothConnected && !mqttConnectionStatusUpdatedToServer && !mqttMessagePublished) {
    updateMqttConnectionStatusToServer();
    mqttMessagePublished=1;
  }

  if(!bluetoothConnected && mqttConnectionStatusUpdatedToServer && !deleteSyncCompleted && !mqttMessagePublished) {
    syncDeletes();
    mqttMessagePublished=1;
  }

  if(!bluetoothConnected  && deleteSyncCompleted && !insertSyncCompleted && !mqttMessagePublished) {
    syncInserts();
    mqttMessagePublished=1;
  }

  //start taking fingerprint if cloud sync is complete
  if(!bluetoothConnected  && insertSyncCompleted && !mqttMessagePublished) {
    uint8_t sensorStatus = takeFingerPrint();
    if(sensorStatus) {
      updateAttendanceToServer(finger.fingerID);
      mqttMessagePublished=1;
    }
  }
 
   //decrementing the mqtt message publish timeout counter if mqtt message is published
   if(!bluetoothConnected && mqttMessagePublished) {
      mqttMessagePublishTimeOutCounter--;
   }

   //resetting the mqtt message publish timeout counter 
   if(!bluetoothConnected && mqttMessagePublishTimeOutCounter == 0) {
      mqttMessagePublished=0;
      mqttMessagePublishTimeOutCounter=MQTT_CONNECTION_TIMEOUT * 10;
   }

  mqtt.loop();
  // printFreeMemory();
  // Serial.println(bluetoothAliveCounter);
  vTaskDelay(pdMS_TO_TICKS(DELAY_PER_ITERATION));
}
