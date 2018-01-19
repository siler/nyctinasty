#include "Nyctinasty_Comms.h"

// mqtt callback is external to class
void MQTTCallback(char* topic, byte* payload, unsigned int length);

#define BUILTIN_LED_ON_STATE LOW
void setOnLED();
void setOffLED();
void toggleLED();

// helpful decodes for the humans.
const String NyctRoleString[] = {
	"Distance",
	"Frequency",
	"Light",
	"Sound",
	"Fx-Simon",
	"Coordinator"
};

// public methods

void NyctComms::begin(NyctRole role, boolean resetRole) {
	Serial << F("Startup. commsBegin starts.") << endl;

	// prep the LED
	pinMode(BUILTIN_LED, OUTPUT);
	digitalWrite(BUILTIN_LED, !BUILTIN_LED_ON_STATE);

	// figure out who I am.
	this->role = role;
	getsetEEPROM(this->role, resetRole);
	String s = (this->sepal == N_SEPAL) ? "" : ("-" + String(this->sepal, 10) );
	String a = (this->side == N_SIDE) ? "" : ("-" + String(this->side, 10) );
	myName = "nyc-" + NyctRoleString[this->role] + s + a;

	// comms setup
	if( WiFi.status()==WL_CONNECTED ) WiFi.disconnect();
	
// variation in WiFi library calls btw ESP8266 and ESP32
#ifdef ARDUINO_ARCH_ESP32
	// station mode
	WiFi.mode(WIFI_MODE_STA);
#endif 
#ifdef ARDUINO_ARCH_ESP8266
	// don't allow the WiFi module to sleep.	
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
	// station mode
	WiFi.mode(WIFI_STA);
#endif 

	mqtt.setClient(wifi);
	mqtt.setCallback(MQTTCallback);

	// enable OTA push programming, too.
	ArduinoOTA.setHostname(myName.c_str());
	ArduinoOTA.onStart([]() {
		Serial.println("Start");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nEnd");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		toggleLED();
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	
	
	Serial << F("Startup. commsBegin ends.") << endl;
}
// subscriptions. cover the messages in Nyctinasty_Messages.h
const String settingsString = "nyc/Setting";
const String distanceString = "nyc/Distance";
const String frequencyString = "nyc/Frequency";
const String simonString = "nyc/Fx/Simon";
const String sep = "/";
void NyctComms::subscribe(SystemCommand *storage, boolean *trueWithUpdate) {
	subscribe(
		settingsString, 
		(void *)storage, trueWithUpdate, 1
	);
}
void NyctComms::subscribe(SimonSystemState *storage, boolean *trueWithUpdate) {
	subscribe(
		simonString, 
		(void *)storage, trueWithUpdate, 0
	);
}
void NyctComms::subscribe(SepalArchDistance *storage, boolean *trueWithUpdate, uint8_t sepalNumber, uint8_t sideNumber) {
	String s = String( (sepalNumber == MY_INDEX) ? this->sepal : sepalNumber, 10 );
	String a = String( (sideNumber == MY_INDEX) ? this->side : sideNumber, 10 );
	subscribe(
		distanceString + sep + s + sep + a, 
		(void *)storage, trueWithUpdate, 0
	);
}
void NyctComms::subscribe(SepalArchFrequency *storage, boolean *trueWithUpdate, uint8_t sepalNumber, uint8_t sideNumber) {
	String s = String( (sepalNumber == MY_INDEX) ? this->sepal : sepalNumber, 10 );
	String a = String( (sideNumber == MY_INDEX) ? this->side : sideNumber, 10 );
	subscribe(
		frequencyString + sep + s + sep + a, 
		(void *)storage, trueWithUpdate, 0
	);
}

// publications.  cover the messages in Nyctinasty_Messages.h
boolean NyctComms::publish(SystemCommand *storage) {
	return publish(
		settingsString, 
		(uint8_t *)storage, (unsigned int)sizeof(SystemCommand)
	);
}
boolean NyctComms::publish(SimonSystemState *storage) {
	return publish(
		simonString, 
		(uint8_t *)storage, (unsigned int)sizeof(SimonSystemState)
	);
}
boolean NyctComms::publish(SepalArchDistance *storage, uint8_t sepalNumber, uint8_t sideNumber) {
	String s = String( (sepalNumber == MY_INDEX) ? this->sepal : sepalNumber, 10 );
	String a = String( (sideNumber == MY_INDEX) ? this->side : sideNumber, 10 );
	return publish(
		distanceString + sep + s + sep + a, 
		(uint8_t *)storage, (unsigned int)sizeof(SepalArchDistance)
	);
}
boolean NyctComms::publish(SepalArchFrequency *storage, uint8_t sepalNumber, uint8_t sideNumber) {
	String s = String( (sepalNumber == MY_INDEX) ? this->sepal : sepalNumber, 10 );
	String a = String( (sideNumber == MY_INDEX) ? this->side : sideNumber, 10 );
	return publish(
		frequencyString + sep + s + sep + a, 
		(uint8_t *)storage, (unsigned int)sizeof(SepalArchFrequency)
	);
}

// call this very frequently
void NyctComms::update() {
	if (WiFi.status() != WL_CONNECTED) {
		connectWiFi();
	} else if (!mqtt.connected()) {
		connectServices();
	} else {
		mqtt.loop(); // look for a message
		ArduinoOTA.handle(); // see if we need to reprogram
	}
}
// check connection
boolean NyctComms::isConnected() {
	return (mqtt.connected()==true) && (WiFi.status()==WL_CONNECTED);
}

// useful functions.
void NyctComms::reboot() {
	setOnLED();

	Serial << endl << F("*** REBOOTING ***") << endl;
	delay(100);
	ESP.restart();
}
void NyctComms::reprogram(String binaryName) {
	setOnLED();
	
	String updateURL = "http://192.168.4.1:80/images/" + binaryName;
	Serial << endl << F("*** RELOADING ***") << endl;
	Serial << F("Getting payload from: ") << updateURL << endl;

	delay(random(10, 500)); // so we all don't hit the server at the same time.

	// enable OTA pull programming.  do not reboot after programming.
	ESPhttpUpdate.rebootOnUpdate(false);

	t_httpUpdate_return ret = ESPhttpUpdate.update(updateURL.c_str());

	switch (ret) {
		case HTTP_UPDATE_FAILED:
			Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
			break;

		case HTTP_UPDATE_NO_UPDATES:
			Serial.println("HTTP_UPDATE_NO_UPDATES");
			// now reboot
			reboot();
			break;

		case HTTP_UPDATE_OK:
			Serial.println("HTTP_UPDATE_OK");
			// now reboot
			reboot();
			break;
	}
}
byte NyctComms::getSepal() { return this->sepal; }
byte NyctComms::getSide() { return this->side; }
byte NyctComms::getSideNext() { return (byte)( ((int)this->side+1) % (int)N_SIDE ); }
byte NyctComms::getSidePrev() { return (byte)( ((int)this->side-1) % (int)N_SIDE ); }

// private methods

typedef struct {
	NyctRole role; 
	byte sepal;
	byte side;
	char wifiPassword[20];
} EEPROMstruct;

void NyctComms::getsetEEPROM(NyctRole role, boolean resetRole) {
	EEPROMstruct save;
	EEPROM.begin(512);
	EEPROM.get(0, save);
	
	if( resetRole || save.role != role) {
		// Yikes.  Fresh uC with no information on its role in life.  
		setOnLED();

		// bootstrap
		Serial << F("*** No role information in EEPROM ***") << endl;
		
		Serial.setTimeout(10);
		
		Serial << F("Enter Role. ") << endl;
		NyctRole n = N_ROLES;
		for( byte i=0; i<n; i++ ) {
			Serial << F("  ") << i << "=" << NyctRoleString[i] << endl;
		}
		while(! Serial.available()) yield();
		String m = Serial.readString();
		save.role = (NyctRole)m.toInt();
		
		Serial << F("Enter Sepal [0-2], ") << N_SEPAL << F(" for N/A.") << endl;
		while(! Serial.available()) yield();
		m = Serial.readString();
		save.sepal = (byte)m.toInt();
		
		Serial << F("Enter Side [0-5], ") << N_SIDE << F(" for N/A.") << endl;
		while(! Serial.available()) yield();
		m = Serial.readString();
		save.side = (byte)m.toInt();

		Serial << F("Enter WiFi Password. ") << endl;
		while(! Serial.available()) yield();
		m = Serial.readString();
		m.toCharArray(save.wifiPassword, sizeof(save.wifiPassword));
	
		EEPROM.put(0, save);
	} 

	EEPROM.end();

	this->role = save.role;
	this->sepal = save.sepal;
	this->side = save.side;
	this->wifiPassword = save.wifiPassword;
	
	Serial << F("Role information in EEPROM:");
	Serial << F(" role=") << this->role;
	Serial << F(" (") << NyctRoleString[this->role] << F(")");
	Serial << F(" sepal=") << this->sepal;
	Serial << F(" side=") << this->side;
	Serial << F(" wifiPassword=") << this->wifiPassword;
	Serial << endl;

}

// memory maintained outside of the class.  :(

byte MQTTnTopics = 0;
String MQTTsubTopic[MQTT_MAX_SUBSCRIPTIONS];
void * MQTTsubStorage[MQTT_MAX_SUBSCRIPTIONS];
boolean * MQTTsubUpdate[MQTT_MAX_SUBSCRIPTIONS];
byte MQTTsubQoS[MQTT_MAX_SUBSCRIPTIONS];

// the real meat of the work is done here, where we process messages.
void MQTTCallback(char* topic, byte* payload, unsigned int length) {

	// String class is much easier to work with
	String t = topic;
	
	// run through topics we're subscribed to
	for( byte i=0;i < MQTTnTopics;i++ ) {
		if( t.equals(MQTTsubTopic[i]) ) {
			// copy memory
			memcpy( MQTTsubStorage[i], (void*)payload, length );
			*MQTTsubUpdate[i] = true;
			
			// toggle the LED when we GET a new message
			toggleLED();
			return;
		}
	}
}

// subscribe to a topic
void NyctComms::subscribe(String topic, void * storage, boolean * updateFlag, uint8_t QoS) {
	// check to see if we've execeed the buffer size
	if( MQTTnTopics >= MQTT_MAX_SUBSCRIPTIONS ) {
		Serial << F("Increase Nyctinasty_Comms.h MQTT_MAX_SUBSCRIPTIONS!!!  Halting.") << endl;
		while(1) yield();
	}
	// queue subscriptions.  actual subscriptions happen at every (re)connect to the broker.
	MQTTsubTopic[MQTTnTopics] = topic;
	MQTTsubStorage[MQTTnTopics] = storage;
	MQTTsubUpdate[MQTTnTopics] = updateFlag; 
	MQTTsubQoS[MQTTnTopics] = QoS;
	MQTTnTopics++;
}

// publish to a topic
boolean NyctComms::publish(String topic, uint8_t * msg, unsigned int msgBytes) {
	// bit of an edge case, but let's check
	if( msgBytes >= MQTT_MAX_PACKET_SIZE ) {
		Serial << F("Increase PubSubClient.h MQTT_MAX_PACKET_SIZE >") << msgBytes << endl;;
		Serial << F("!!!  Halting.") << endl;
		while(1) yield();
	}
	// bail out if we're not connected
	if( !this->isConnected() ) return( false );
	// toggle the LED when we send a new message
	toggleLED();
	// ship it
	return mqtt.publish(topic.c_str(), msg, msgBytes);
}



// connect to the WiFi
void NyctComms::connectWiFi(String ssid, unsigned long interval) {
	static Metro connectInterval(interval);
	static byte retryCount = 0;
	
	if (WiFi.status()==WL_CONNECTED) retryCount = 0;
	
	if ( connectInterval.check() ) {
		retryCount++;
		
		Serial << F("WiFi status=") << WiFi.status();
		Serial << F("\tAttempting WiFi connection to ") << ssid << F(" password ") << this->wifiPassword << endl;
		// Attempt to connect
		WiFi.begin(ssid.c_str(), this->wifiPassword.c_str());
		
		connectInterval.reset();
	}
	
	if( retryCount >= 10 ) reboot();
}

// connect to MQTT broker and other services
void NyctComms::connectServices(String broker, word port, unsigned long interval) {
	static Metro connectInterval(interval);
	static byte retryCount = 0;

	if ( connectInterval.check() ) {

		retryCount++;

		if ( MDNS.begin ( myName.c_str() ) ) {
			Serial << F("mDNS responder started: ") << myName << endl;
		} else {
			Serial << F("Could NOT start mDNS!") << endl;
		}
		
		Serial << F("OTA started: ") << myName << endl;
		ArduinoOTA.begin();
		
		Serial << F("Attempting MQTT connection as ") << myName << " to " << broker << ":" << port << endl;
		mqtt.setServer(broker.c_str(), port);

		// Attempt to connect
		if (mqtt.connect(myName.c_str())) {
			Serial << F("Connected.") << endl;

			// (re)subscribe
			for(byte i=0; i<MQTTnTopics; i++) {
				Serial << F("Subscribing: ") << MQTTsubTopic[i];
				Serial << F(" QoS=") << MQTTsubQoS[i];
				mqtt.loop(); // in case messages are in.
				boolean ret = mqtt.subscribe(MQTTsubTopic[i].c_str(), MQTTsubQoS[i]);
				Serial << F(". OK? ") << ret << endl;
			}
			
			retryCount = 0;

		} else {
			Serial << F("Failed. state=") << mqtt.state() << endl;
			Serial << F("WiFi status connected? ") << (WiFi.status()==WL_CONNECTED) << endl;
		}

		connectInterval.reset();
	}

	if( retryCount >= 10 ) reboot();
}


boolean ledState = !BUILTIN_LED_ON_STATE;
void setOnLED() {
	if( ledState!=BUILTIN_LED_ON_STATE ) toggleLED();
}
void setOffLED() {
	if( ledState==BUILTIN_LED_ON_STATE ) toggleLED();
}
void toggleLED() {
	// toggle the LED when we process a message
	ledState = !ledState;
	digitalWrite(BUILTIN_LED, ledState);
}


/*


// save
String myID, otaID;



	// subscribe to a topic, provide storage for the payload, provide a flag for update indicator. I considered the use of a callback function, but the FSM context suggests that the message handling should reference the FSM state, which would yield branching logic in the handler anyway.
	template <class T>
	void commsSubscribe(String topic, T * msg, boolean * updateFlag, uint8_t QoS=0) {
		commsSubscribe(topic, (void *)msg, updateFlag, QoS);
	}

	// publish to a topic
	template <class T>
	boolean commsPublish(String topic, T * msg) {
		return commsPublish(topic, (uint8_t *)msg, (unsigned int)sizeof(T));
	}


	// some led accessors
	// work


	void getEEPROM();
	void putEEPROM();

	void commsSubscribe(String topic, void * msg, boolean * updateFlag, uint8_t QoS);
	boolean commsPublish(String topic, uint8_t * msg, unsigned int msgBytes);

	// don't need to mess with these
	void connectWiFi(String ssid="GamesWithFire", String passwd="safetythird", unsigned long interval=5000UL);
	void connectServices(String broker="192.168.4.1", word port=1883, unsigned long interval=500UL);




// build an MQTT id, which must be unique.
String commsIdSepalArchFrequency(byte sepalNumber, byte archNumber);
String commsIdSepalCoordinator(byte sepalNumber);
String commsIdSepalArchLight(byte sepalNumber, byte archNumber);
String commsIdFlowerSimonBridge();


// build a MQTT topic, for use with subscribe and publish routines.
#define ALL
String commsTopicSystemCommand();
String commsTopicLight(byte sepalNumber, byte archNumber);
String commsTopicDistance(byte sepalNumber, byte archNumber);
String commsTopicFrequency(byte sepalNumber, byte archNumber);
String commsTopicFxSimon();

// internal use?
String commsStringConstructor(String topicOrId, byte sepalNumber=N_SEPALS, byte archNumber=N_ARCHES);


// ID and topics
String commsIdSepalArchFrequency(byte s, byte a) { return commsStringConstructor("Frequency", s, a); }
String commsIdSepalCoordinator(byte s) { return commsStringConstructor("Coordinator", s, N_ARCHES); }
String commsIdSepalArchLight(byte s, byte a) { return commsStringConstructor("FxSimon", s, a); }
String commsIdFlowerSimonBridge() { return commsStringConstructor("Fx-Simon"); }

// subscription topics
// note that wildcards are not supported, as we use string matching to determine incoming messages
String commsTopicSystemCommand() { return stringConstructor("Settings"); }
String commsTopicLight(byte s, byte a) { return stringConstructor("Light", s, a); }
String commsTopicDistance(byte s, byte a) { return stringConstructor("Dist", s, a); }
String commsTopicFrequency(byte s, byte a) { return stringConstructor("Freq", s, a); }
String commsTopicFxSimon() { return stringConstructor("Fx/Simon"); }

String stringConstructor(String topic, byte sepalNumber, byte archNumber) {
	const String project = "nyc";
	const String sep = "/";
	
	String sepal = (sepalNumber < N_SEPALS) ? ( sep + String(sepalNumber,10) ) : "";
	String arch = (archNumber < N_ARCHES && sepalNumber < N_SEPALS) ? ( sep + String(archNumber,10) ) : "";

	String sub = project + sep + topic + sepal + arch;
//	Serial << sub << endl;
	return sub;
}

// reboot
void reboot() {
}

// reprogram
void reprogram(String binaryName) {
	
}


*/