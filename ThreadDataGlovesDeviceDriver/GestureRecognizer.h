#pragma once
#include <string>
#include <Windows.h>
#include <boost/circular_buffer.hpp>
#define TIME_SERIES_SIZE 1000

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

struct SensorInfo {
	float finger_sensors[5];
	float gyroscope[3];
	float accelerometer[3];
} typedef SensorInfo;

class GestureRecognizer
{
public:
	GestureRecognizer(HANDLE *heapPtr);
	~GestureRecognizer();
	Gesture *recognize();
	void setCalibrationWithFile(std::string file_path);
	void setCalibrationWithData(CalibrationInfo data);
	bool isCalibrationSet();
	void GestureRecognizer::addToTimeSeries(SensorInfo s);

private:
	bool calibrationSet;
	CalibrationInfo calibrationInfo;
	HANDLE* heapPtr;
	boost::circular_buffer<SensorInfo> *timeSeriesData;
};

