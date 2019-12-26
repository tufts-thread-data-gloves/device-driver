#pragma once

struct SensorInfo {
	float finger_sensors[5];
	float gyroscope[3];
	float accelerometer[3];
} typedef SensorInfo;

typedef void (*listenCallback)(SensorInfo arg);

class BluetoothManager
{
public:
	BluetoothManager(bool mock);
	~BluetoothManager();
	void findGlove(listenCallback c);
	void setConnected(bool c);

private:
	bool connected;
	bool mock; //temporary
};

