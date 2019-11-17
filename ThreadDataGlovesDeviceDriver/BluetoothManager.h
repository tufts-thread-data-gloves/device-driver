#pragma once
#include <string.h>

using namespace std;

struct Glove {
	string bluetooth_addr;
	// other information
} typedef Glove;

struct SensorInfo {
	double finger_sensors[5];
	double gyroscope[3];
	double accelerometer[3];
} typedef SensorInfo;

class BluetoothManager
{
public:
	BluetoothManager();
	~BluetoothManager();
	Glove findGlove(); // blocking function
	SensorInfo listen();

private:

};

