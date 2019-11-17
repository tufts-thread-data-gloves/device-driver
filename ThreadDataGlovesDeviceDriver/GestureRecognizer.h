#pragma once
#include "BluetoothManager.h"

// CalibrationInfo stores max reading from strain sensors and min reading
struct CalibrationInfo {
	double maxReading[5];
	double minReading[5];
} typedef CalibrationInfo;

// Stores data about gesture made
struct Gesture {
	GestureCode gestureCode;
	int x;
	int y;
	int z;
} typedef Gesture;

enum GestureCode {
	ZOOM_IN=1, ZOOM_OUT, ROTATE, PAN 
};

class GestureRecognizer
{
public:
	GestureRecognizer(BluetoothManager* bluetoothManager);
	~GestureRecognizer();
	Gesture recognize(); // this is blocking; will wait until gesture is made
	void setCalibrationWithFile(string file_path);
	void setCalibrationWithData(CalibrationInfo data);
	void setGlove(Glove g);

private:
	bool calibrationSet;
	Glove glove;
	CalibrationInfo calibrationInfo;
	BluetoothManager* bluetoothMngr;
};

