#include "GestureRecognizer.h"
#include "BluetoothManager.h"
#include <Windows.h>


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
	Sleep(1000); // to mimic pausing between gestures
	Gesture g = { PAN, 1, 1, 1 };
	return g;
}

void GestureRecognizer::setCalibrationWithFile(string file_path) {
	// no-op for now
}

void GestureRecognizer::setCalibrationWithData(CalibrationInfo data) {
	// no-op fornow
}

void GestureRecognizer::setGlove(Glove g) {
	glove = g;
}