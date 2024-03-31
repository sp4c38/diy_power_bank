#include <ArduinoLog.h>

#include "ble_app.h"

BLEApp::BLEApp():
	ledService("19B10010-E8F2-537E-4F6C-D104768A1214"),
	ledButtonCharacteristic("19B10011-E8F2-537E-4F6C-D104768A1214", BLERead | BLEWrite | BLENotify) {
}

void BLEApp::setup() {
	if (!BLE.begin()) {
		Log.fatalln("Starting BLE failed.");
		exit(1);
	}
	ledService.addCharacteristic(ledButtonCharacteristic);
	BLE.addService(ledService);
	
	BLE.setLocalName("Power bank");
	BLE.setAdvertisedService(ledService);
	
	BLE.advertise();
	Log.noticeln("Started BLE advertising.");
	
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);
}

void BLEApp::loop() {
	BLE.poll();
	
	bool ledOn = ledButtonCharacteristic.value();
	if (ledOn) {
		digitalWrite(LED_BUILTIN, HIGH);
	} else {
		digitalWrite(LED_BUILTIN, LOW);
	}
	
	BLEDevice central = BLE.central();
	if (central.connected()) {
		Log.noticeln("Connected to central %s.", central.address().c_str());
	} else {
		Log.noticeln("Disconnected central.");
	}
}