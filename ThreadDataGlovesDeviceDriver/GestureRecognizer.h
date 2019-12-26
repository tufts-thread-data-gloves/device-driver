#pragma once
#include "BluetoothManager.h"
#include <string>

// CalibrationInfo stores max reading from strain sensors and min reading
struct CalibrationInfo {
	double maxReading[5];
	double minReading[5];
} typedef CalibrationInfo;

enum GestureCode {
	ZOOM_IN = 1, ZOOM_OUT, ROTATE, PAN
};

// Stores data about gesture made
struct Gesture {
	GestureCode gestureCode;
	int x;
	int y;
	int z;
} typedef Gesture;

class GestureRecognizer
{
public:
	GestureRecognizer(BluetoothManager* bluetoothManager);
	~GestureRecognizer();
	Gesture recognize(); // this is blocking; will wait until gesture is made
	void setCalibrationWithFile(std::string file_path);
	void setCalibrationWithData(CalibrationInfo data);

private:
	bool calibrationSet;
	CalibrationInfo calibrationInfo;
	BluetoothManager* bluetoothMngr;
};

