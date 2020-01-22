#pragma once

struct SensorInfo {
	float finger_sensors[5];
	float gyroscope[3];
	float accelerometer[3];
} typedef SensorInfo;

typedef void (*listenCallback)(SensorInfo arg);
typedef void (*errorCallback)();

class BluetoothManager
{
public:
	BluetoothManager();
	~BluetoothManager();
	void findGlove(listenCallback c, errorCallback e);
	void setConnected(bool c);

private:
	bool connected;
};

