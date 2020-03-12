#include "GestureRecognizer.h"
#include "BluetoothManager.h"
#include <iostream>
#include <fstream>

#define LINE_SIZE 250

using namespace std; 

/*
Gesture recognizer is not yet done, for now this class is meant to hold the time series data and record gestures
*/

std::mutex GestureRecognizer::recordingOnLock;
std::mutex GestureRecognizer::queueLock;

GestureRecognizer::GestureRecognizer(HANDLE *heapPtr) {
	heapPtr = heapPtr;
	timeSeriesData = new boost::circular_buffer<SensorInfo>(TIME_SERIES_SIZE);

	// Initialize thread that is constantly recording data and timestamps
	std::thread IORecordingThread(&GestureRecognizer::IORecordingThreadFunc, this);
	IORecordingThread.detach();
}

GestureRecognizer::~GestureRecognizer() {

}

/*
 * recognize is used to perform recognizer on current time series data
 * Returns gesture if gesture was recognized, otherwise returns NULL
 */
Gesture *GestureRecognizer::recognize() {
	/*// this is mocking finding a gesture for now
	double random = rand() / double(RAND_MAX);
	if (random > 0.5) {
		Gesture *g = (Gesture*)HeapAlloc(*(heapPtr), HEAP_ZERO_MEMORY, sizeof(Gesture));
		if (g == NULL)
			return NULL;
		g->x = 1;
		g->y = 1;
		g->z = 1;
		return g;
	}*/
	// TODO: check if we are calibrated, then perform gesture recognition
	return NULL;
}

void GestureRecognizer::zeroSavedCalibration() {
	calibrationSet = false;
	// clear buffer
	timeSeriesData->clear();
}

void GestureRecognizer::setCalibrationWithData(CalibrationInfo data) {
	calibrationSet = true;
	calibrationInfo = data;
	printf("Calibration data set in gesturerecognizer \n");
}

bool GestureRecognizer::isCalibrationSet() {
	return calibrationSet;
}

CalibrationInfo GestureRecognizer::getCalibrationInfo() {
	return calibrationInfo;
}

/*
 * addToTimeSeries adds new time data to circular ring buffer
 * Input: SensorInfo struct
 */
void GestureRecognizer::addToTimeSeries(SensorInfo s) {
	timeSeriesData->push_back(s);
	if (isRecording) {
		numberOfElementsRecorded++;
	}
	else {
		queueLock.lock();
		recordingQueue.push(s);
		queueLock.unlock();
	}
}

// Temporary calls for recording gestures
// Returns false if we cant start recording, true otherwise
bool GestureRecognizer::startRecording() {
	if (!isRecording && calibrationSet) {
		numberOfElementsRecorded = 0;
		isRecording = true;
		recordingOnLock.lock();
		return true;
	}
	else {
		return false; 
	}
}
bool GestureRecognizer::endRecording(char* filepath) {
	if (isRecording) {
		// get current end, save all the data from startingPoint to current end in file asynchoronously to not slow down main program
		ofstream outfile;
		outfile.open(filepath, std::fstream::out | std::fstream::app);

		if (outfile.bad()) {
			printf("Could not open file\n");
			return false;
		}

		printf("We recorded %d elements\n", numberOfElementsRecorded);
		int endIndex = timeSeriesData->end() - timeSeriesData->begin();
		for (int i = endIndex - numberOfElementsRecorded; i < endIndex; i++) {
			// write each sensor info to a line on the filepath - each gets its own cell

			// write each sensor info to a "cell" on a line in the filepath
			// form string to write
			char sensorInfoString[LINE_SIZE];
			SensorInfo elt;
			try {
				elt = timeSeriesData->at(i);
			}
			catch (...) {
				printf("Exception caught \n");
				return false;
			}
			int n = sprintf_s(sensorInfoString, LINE_SIZE, "%4.2f,%4.2f,%4.2f,%4.2f,%4.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f\n",
				elt.finger_sensors[0], elt.finger_sensors[1], elt.finger_sensors[2], elt.finger_sensors[3], elt.finger_sensors[4],
				elt.accelerometer[0], elt.accelerometer[1], elt.accelerometer[2],
				elt.magnometer[0], elt.magnometer[1], elt.magnometer[2]);
			if (n < 0) {
				printf("Could not do sprintf_s \n");
				return false;
			}

			// now write it to the file
			outfile.write(sensorInfoString, n);
		}
		// at the end, write a newline
		outfile.write("\n", 1);

		//printf("Should have completed writing to file\n");
		isRecording = false;
		recordingOnLock.unlock();
		return true;
	}
	else {
		printf("Was not recording \n");
		return false;
	}
}

void GestureRecognizer::IORecordingThreadFunc() {
	// open file that we are going to write to
	ofstream outfile;
	outfile.open("C:\\Users\\Aaron\\source\\repos\\python-api-device-driver\\gestureRecordings\\noiseRecording.txt", std::fstream::out | std::fstream::app);

	if (outfile.bad()) {
		printf("Could not open file\n");
		return;
	}


	while (true) {
		// acquire lock
		// check if we are recording or not
		// write to the file

		recordingOnLock.lock();
		if (isRecording) {
			recordingOnLock.unlock();
			continue;
		}

		// get data to write
		queueLock.lock();
		if (recordingQueue.empty()) {
			queueLock.unlock();
			recordingOnLock.unlock();
			continue;
		}
		else {
			SensorInfo elt = recordingQueue.front();
			recordingQueue.pop();
			queueLock.unlock();

			char sensorInfoString[LINE_SIZE];
			int n = sprintf_s(sensorInfoString, LINE_SIZE, "%4.2f,%4.2f,%4.2f,%4.2f,%4.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f,%5.2f\n",
				elt.finger_sensors[0], elt.finger_sensors[1], elt.finger_sensors[2], elt.finger_sensors[3], elt.finger_sensors[4],
				elt.accelerometer[0], elt.accelerometer[1], elt.accelerometer[2],
				elt.magnometer[0], elt.magnometer[1], elt.magnometer[2]);

			// now write it to the file
			outfile.write(sensorInfoString, n);
		}
		recordingOnLock.unlock();
	}
}