#ifndef Nyctinasty_Comms_h
#define Nyctinasty_Comms_h

#include <Arduino.h>

#include <Streaming.h>
#include <Metro.h>
#include <EEPROM.h>

// variation in WiFi library names btw ESP8266 and ESP32
#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#endif 
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#endif 

#include <ESP8266httpUpdate.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include "Nyctinasty_Messages.h"

typedef struct {
	char role[20]; // very important to NOT use String (doesn't have a defined length)
	byte sepal={N_SEPALS};
	byte arch={N_ARCHES};
	
	uint32_t checksum = 8675309; // don't lose my number
} Id;

Id getIdEEPROM();
void putIdEEPROM(Id id);

// call this very frequently
void commsUpdate();

// check connection
boolean commsConnected(); 

// don't need to mess with these
void connectWiFi(String ssid="GamesWithFire", String passwd="safetythird", unsigned long interval=5000UL);
void connectMQTT(String broker="192.168.4.1", word port=1883, unsigned long interval=500UL);
void commsCallback(char* topic, byte* payload, unsigned int length);
void commsSubscribe(String topic, void * msg, boolean * updateFlag, uint8_t QoS);
boolean commsPublish(String topic, uint8_t * msg, unsigned int msgBytes);
void setOnLED();
void setOffLED();
void toggleLED();

// build an MQTT id, which must be unique.
String commsIdSepalArchFrequency(byte sepalNumber, byte archNumber);
String commsIdSepalCoordinator(byte sepalNumber);
String commsIdSepalArchLight(byte sepalNumber, byte archNumber);
String commsIdFlowerSimonBridge();

// startup.  use unique id that's built by the commsId* helpers. 
Id commsBegin(boolean resetRole=false, byte ledPin=BUILTIN_LED);

// build a MQTT topic, for use with subscribe and publish routines.
#define ALL
String commsTopicSystemCommand();
String commsTopicLight(byte sepalNumber, byte archNumber);
String commsTopicDistance(byte sepalNumber, byte archNumber);
String commsTopicFrequency(byte sepalNumber, byte archNumber);
String commsTopicFxSimon();

// internal use?
String commsStringConstructor(String topicOrId, byte sepalNumber=N_SEPALS, byte archNumber=N_ARCHES);

// subscribe to a topic, provide storage for the payload, provide a flag for update indicator
// I considered the use of a callback function, but the FSM context suggests that the 
// message handling should reference the FSM state, which would yield branching logic
// in the handler anyway.
template <class T>
void commsSubscribe(String topic, T * msg, boolean * updateFlag, uint8_t QoS=0) {
	commsSubscribe(topic, (void *)msg, updateFlag, QoS);
}

// publish to a topic
template <class T>
boolean commsPublish(String topic, T * msg) {
	return commsPublish(topic, (uint8_t *)msg, (unsigned int)sizeof(T));
}

// useful functions
void reboot();
void reprogram(String binaryName);

#endif

