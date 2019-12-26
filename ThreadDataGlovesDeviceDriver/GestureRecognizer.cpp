#include "GestureRecognizer.h"
#include "BluetoothManager.h"

using namespace std; 

/*
FOR NOW: this is a mock class
*/

GestureRecognizer::GestureRecognizer(BluetoothManager* bluetoothManager) {
	// for now, we ignore  calibration
	bluetoothMngr = bluetoothManager;
}

GestureRecognizer::~GestureRecognizer() {

}

Gesture GestureRecognizer::recognize() {
	//Sleep(1000); // to mimic pausing between gestures
	Gesture g = { PAN, 1, 1, 1 };
	g.x = 1;
	g.y = 1;
	g.z = 1;
	return g;
}

void GestureRecognizer::setCalibrationWithFile(string file_path) {
	// no-op for now
}

void GestureRecognizer::setCalibrationWithData(CalibrationInfo data) {
	// no-op fornow
}