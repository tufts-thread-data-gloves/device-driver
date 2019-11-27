#include "BluetoothManager.h"
#include "Windows.h"

/*
FOR NOW: this is a mock class for testing network functionality with interprocess communication

PLAN: this class will connect with bluetooth billboard for glove, and constantly pull information from
its services and return them in the listen function
*/

BluetoothManager::BluetoothManager() {
	// no-op
}

BluetoothManager::~BluetoothManager() {
	// no-op
}

Glove BluetoothManager::findGlove() {
	// pretend we found glove
	Glove g;
	g.bluetooth_addr = "023t2wgnkrsdv";
	return g;
}

SensorInfo BluetoothManager::listen() {
	// pretend like we got sensor info
	SensorInfo si = { {1, 1, 1, 1, 1}, {1,1,1}, {1,1,1} };
	return si;
}