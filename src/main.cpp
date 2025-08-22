// https://github.com/vincentj87/mkm-io.git

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
// === Ethernet Settings ===
#define ETH_RST 25 // Ethernet Reset Pin
#define ETH_SPI_SCS 5 // W5500 CS
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEF};
// === ONE SEGMENT WITH SERVER CONFIG IP ===
// IPAddress ip(172,17,210,234);
// IPAddress gateway(172,17,210,254);
// IPAddress subnet(255, 255,255,0);
// IPAddress mqttServer(172,17,210,59);

// === ONE SEGMENT WITH SWITCH CONFIG IP ===
IPAddress ip(172, 19, 16, 61);
IPAddress gateway(172, 19, 17, 254);
IPAddress subnet(255, 255, 254, 0);
IPAddress mqttServer(172, 19, 16, 243);
EthernetClient ethClient;
// === RFID Settings ===
#include "UNIT_UHF_RFID.h"
HardwareSerial SerialRFID(1);
Unit_UHF_RFID uhf;
String rfidReadResult = "";
// === Bluetooth Settings ===
#include <BluetoothSerial.h>
BluetoothSerial SerialBT;
String barcodeReadBuffer="";
bool isBarcodeConnected = false, prevBarcodeConnected = false;
String barcodeReadResult = "";
// aa:a8:02:05:55:bd kassen mac
uint8_t kassen_mac[] = {0xAA, 0xA8, 0x02, 0x05, 0x55, 0xBD}; // Example MAC address for the RFID reader
unsigned long lastReadRFID = 0;

// === MQTT Settings ===
#define MQTT_Port 1883
#define MQTT_HearbeatDuration 1000
#define telemetryTopic "mkm/up/pairing_station/telemetry" // MQTT topic for sending data
#define heartbeatTopic "mkm/up/pairing_station/heartbeat" // MQTT topic for heartbeat
void callback(char *topic, byte *payload, unsigned int length);
PubSubClient mqttClient(mqttServer, MQTT_Port, callback, ethClient);
const char *mqtt_username = "admin";
const char *mqtt_password = "admin";
unsigned long MQTT_nowT;
// IPAddress dns(127, 0, 0, 54); old xapiens dns , no longer used in MKM site

// === TFT Pins ===
#define TFT_CS 33
#define TFT_DC 26
#define TFT_RST 27
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// === Button Pins ===
#define BUTTON1_PIN 36
#define BUTTON2_PIN 39
bool button1WasPressed = false,button2WasPressed=false;

unsigned long nowT = 0;

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}
void barcodeMantain()
{

  if (!SerialBT.connected())
  {
    Serial.println("barcodeMaintain(): Bluetooth not connected. Attempting to reconnect...");
    if (SerialBT.connect(kassen_mac))
    {
      Serial.println(" barcodeMaintain(): Bluetooth reconnected successfully!");
    }
    else
    {
      Serial.println(" barcodeMaintain(): Failed to reconnect Bluetooth.");
      delay(1000); // Wait before retrying
    }
  }
  
}

void barcodeBegin()
{

  SerialBT.begin("MKM-Pairing-Station", true);
  Serial.println("Bluetooth initialized");

}
void tftShowBarcode(){
// print barcode read result
  tft.setTextColor(ILI9341_WHITE);
  tft.fillRect(0,130,320,30,ILI9341_BLACK);
  tft.setCursor(10, 130);
  tft.print("BRCD");
  tft.setCursor(90,130);
  tft.print(barcodeReadResult);

  
}
void tftShowRfid(){
  // print rfid read result 
  tft.setTextColor(ILI9341_WHITE);
  tft.fillRect(0,160,320,30,ILI9341_BLACK);
  tft.setCursor(10, 160);
  tft.print("RFID");
  tft.fillRect(0,190,320,30,ILI9341_BLACK);
  tft.setCursor(10,190);
  tft.print(rfidReadResult);
}
void barcodeRead()
{
  while (SerialBT.available()){
    char c = SerialBT.read();
    if(c!='\r'){
      barcodeReadBuffer+=c;
    } 
    
  }
  if(barcodeReadBuffer.length()>0){
    Serial.println("Barcode Read: " + barcodeReadBuffer);
    barcodeReadResult = barcodeReadBuffer;
    barcodeReadBuffer="";
    tftShowBarcode();
  }    
}

void rfidRead()
{

    uint8_t result = uhf.pollingMultiple(20);
    if (result > 0)
    {
      for (uint8_t i = 0; i < result; i++)
      {
        Serial.println("pc: " + uhf.cards[i].pc_str);
        Serial.println("rssi: " + uhf.cards[i].rssi_str);
        Serial.println("epc: " + uhf.cards[i].epc_str);
        Serial.println("-----------------");
        rfidReadResult = uhf.cards[i].epc_str;
        tftShowRfid();
      }
    }
}



void rfidSetup()
{
  uhf.begin(&SerialRFID, 115200, 16, 17, false);
  uhf.setTxPower(2600);
  delay(1000);
  if (uhf.getVersion() != "ERROR")
  {
    tft.fillRect(0, 70, 320, 30, ILI9341_BLACK);
    tft.setCursor(10, 70);
    tft.setTextColor(ILI9341_GREEN);
    tft.print("RFID OK");
    Serial.println("RFID Module initialized successfully.");
  }
  else
  {
    tft.fillRect(0, 70, 320, 40, ILI9341_BLACK);
    tft.setCursor(10, 70);
    tft.setTextColor(ILI9341_RED);
    tft.print("RFID ERROR");
    Serial.println("Failed to initialize RFID Module.");
  }
}
void connectToMQTT()
{
  while (!mqttClient.connected())
  {
    Serial.println("Trying to connect to mqtt server");
    if (mqttClient.connect("ESP32Client", mqtt_username, mqtt_password))
    {
      Serial.println("connected!");
      mqttClient.subscribe("test/topic");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 3 seconds"); 
      delay(3000);
    }
  }
}
void heartBeatRoutine()
{
  if ((MQTT_nowT + 3000) <= millis())
  {
    MQTT_nowT = millis();

    mqttClient.publish(heartbeatTopic, "{\"heartbeat\":1}");
  }
}
void HomePage()
{

  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setTextSize(2);

  tft.setCursor(10, 10);
  tft.println("MKM PAIRING STATION");

  tft.setCursor(10, 40);
  tft.print("OTA IP: ");
  tft.println(Ethernet.localIP());
  delay(3000);
  tft.fillScreen(ILI9341_BLACK);
}
void buttonInit()
{
  pinMode(BUTTON1_PIN, INPUT); // Using external pull-up
  pinMode(BUTTON2_PIN, INPUT);
}
void ConnectivityStatusTFT(){
  // bluetooth status
  if(!SerialBT.connected())
  {
    Serial.println("Reconnected to Zebra scanner!");
    tft.fillRect(0, 10, 320, 16, ILI9341_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(ILI9341_RED);
    tft.print("KASSEN : DISCONNECTED");
  }
  else
  {
    tft.fillRect(0, 10, 320, 16, ILI9341_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(ILI9341_GREEN);
    tft.print("KASSEN : CONNECTED");
    Serial.println("Connectivity Status (): Bluetooth not connected, trying to reconnect...");
  }
  
  // mqtt status
  if(!mqttClient.connected())
  {
    tft.fillRect(0, 40, 320, 20, ILI9341_BLACK);
    tft.setCursor(10, 40);
    tft.setTextColor(ILI9341_RED);
    tft.print("MQTT: NOT CONNECTED");
  }
  else
  {
    tft.fillRect(0, 40, 320, 20, ILI9341_BLACK);
    tft.setCursor(10, 40);
    tft.setTextColor(ILI9341_GREEN);
    tft.print("MQTT: CONNECTED");
  }
}
void buttonRoutine()
{ 
  //clearing buffer
  if(digitalRead(BUTTON2_PIN)==LOW){
    if (!button2WasPressed){
      rfidReadResult="";
      barcodeReadResult="";
      tftShowBarcode();
      tftShowRfid();
    }
    button2WasPressed=true;
  }else{
    button2WasPressed=false;
  }
  if (digitalRead(BUTTON1_PIN) == LOW)
  {
    if (!button1WasPressed)
    {
      button1WasPressed = true;
      if (rfidReadResult != "" and barcodeReadResult != "")
      {
        // formating to json format
        StaticJsonDocument<200> doc;
        doc["rfid"] = rfidReadResult;
        doc["barcode"] = barcodeReadResult;
        String jsonString;
        serializeJson(doc, jsonString);
        Serial.println("Sending data to MQTT: " + jsonString);
        if (mqttClient.connected())
        { 
          mqttClient.publish(telemetryTopic, jsonString.c_str());
          tft.fillRect(0,100, 320, 20, ILI9341_BLACK);
          tft.setCursor(10,100);
          tft.setTextColor(ILI9341_GREEN);
          tft.print("Data sent to MQTT");
          barcodeReadResult="";
          rfidReadResult="";
          tftShowBarcode();
          tftShowRfid();
          delay(2000);
          tft.fillRect(0,100, 320, 20, ILI9341_BLACK);

        }
        else
        {
          tft.fillRect(0, 100, 320, 20, ILI9341_BLACK);
          tft.setCursor(10, 100);
          tft.setTextColor(ILI9341_RED);
          tft.print("MQTT not connected");
        }
      }
    }
  }
  else
  {
    button1WasPressed = false;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
 
  // === TFT INIT ===
  Serial.println("TFT Begin");
  tft.begin();

  // === Ethernet INIT ===2
  delay(1000);
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW); // Reset Ethernet module
  delay(2000);
  digitalWrite(ETH_RST, HIGH); // Release reset
  delay(1000);
  Serial.println("ETH init");
  Ethernet.init(ETH_SPI_SCS);
  Ethernet.begin(mac, ip, gateway, subnet);
  
  // === HOME PAGE ===
  Serial.println("Home page");
  HomePage();

  // === ETHERNET STATUS ===
  Serial.println("Ethernet check");

  // === RFID INIT ===
  Serial.println("Rfid init ");
  rfidSetup();
  // === OTA INIT ===
  Serial.println("Ota init");
  ArduinoOTA.begin(Ethernet.localIP(), "arduino", "password", InternalStorage);
  // === Bluetooth INIT ===
  Serial.println("Barcode begin");
  barcodeBegin();
  // === MQTT INIT ===
  Serial.println("MQTT Init");
  mqttClient.setServer(mqttServer, MQTT_Port);
  Serial.println("OTA Ready");

  // === BUTTON INIT ===
  Serial.println("button init ");
  buttonInit();
  
   // RTOS init 
}

void loop()
{
  
  if (mqttClient.connected())
  {
    if( (nowT+500) <= millis())
    {
      
      ConnectivityStatusTFT();
      barcodeRead();
      rfidRead();
      barcodeMantain();
      nowT = millis();
      
    }
    heartBeatRoutine();
    buttonRoutine();
    
    
  }
  else{
    connectToMQTT();
  }
  ArduinoOTA.handle();

  mqttClient.loop();
  delay(10); // Small delay to avoid blocking the loop
}
