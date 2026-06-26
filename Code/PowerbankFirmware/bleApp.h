/*
The ble_app assists in performing different functionality that is better separated
from the rest of the application.
*/

#include <ArduinoBLE.h>

class BLEApp {
	public:
		BLEService ledService;
		BLEBooleanCharacteristic ledButtonCharacteristic;
	
		BLEApp();
		void setup();
		void loop();
};
