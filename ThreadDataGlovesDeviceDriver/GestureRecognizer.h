#pragma once
#include <Windows.h>
#include <boost/circular_buffer.hpp>
#define TIME_SERIES_SIZE 10000

// CalibrationInfo stores max reading from strain sensors and min reading
struct CalibrationInfo {
	uint16_t maxReading[5];
	uint16_t minReading[5];
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
	float *finger_sensors; // array of 5
	float *accelerometer; // array of 3
	float *magnometer; // array of 3
} typedef SensorInfo;

class GestureRecognizer
{
public:
	GestureRecognizer(HANDLE *heapPtr);
	~GestureRecognizer();
	Gesture *recognize();
	void setCalibrationWithData(CalibrationInfo data);
	bool isCalibrationSet();
	CalibrationInfo getCalibrationInfo();
	void GestureRecognizer::addToTimeSeries(SensorInfo s);
	void GestureRecognizer::zeroSavedCalibration();

	// Temporary calls for recording gestures
	bool startRecording();
	bool endRecording(char *filepath);

private:
	bool calibrationSet;
	CalibrationInfo calibrationInfo;
	HANDLE* heapPtr;
	boost::circular_buffer<SensorInfo> *timeSeriesData;

	// Temporary variables for recording gestures
	bool isRecording;
	boost::circular_buffer<SensorInfo>::iterator startingPoint;

};

