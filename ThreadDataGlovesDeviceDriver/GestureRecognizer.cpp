#include "GestureRecognizer.h"
#include "BluetoothManager.h"
#include <iostream>
#include <fstream>

#define LINE_SIZE 250

using namespace std;

// constants for thresholding when the glove is in a fist (these are experimental values)
const float fingerOneThresh = 0.4035;
const float fingerTwoThresh = 0.4;
const float fingerThreeThresh = 0.38;
const float stableAxis = 185;
const float accelerometerUpperThreshZ = 1.3;
const float accelerometerLowerThreshZ = 0.6;
const float accelerometerUpperThreshY = -0.1;
const float accelerometerLowerThreshY = -0.8;


bool fingersInFist(SensorInfo elt);
bool outOfFist(SensorInfo eltA, SensorInfo eltB);
bool closeTo(float a, float b);
bool decideGesture(SensorInfo eltA, SensorInfo eltB, Gesture* gesture);
int sign(int x);
bool gyroscopeStable(const char* axes, SensorInfo eltA, SensorInfo eltB);
bool accelerometerStable(SensorInfo eltA, SensorInfo eltB);
bool inPan(const char* axis, SensorInfo eltA, SensorInfo eltB);

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
	printf("In recocgnizer, is cal set %d\n", calibrationSet);
	if (calibrationSet) {
		// are we in a gesture? If so, see if we can determine the gesture or if it has ended, if not check if we have started a gesture
		if (inGesture) {
			// check if still in gesture - we say the gesture has stopped when at least 
			// two of the three fingers are not in the fist threshold for two straight timestamps
			int endIndex = timeSeriesData->end() - timeSeriesData->begin();
			if (outOfFist(timeSeriesData->at(endIndex - 1), timeSeriesData->at(endIndex - 2))) {
				inGesture = false;
				return NULL;
			}
			// still in gesture - determine if we know what type of gesture it is
			static Gesture g;
			if (decideGesture(timeSeriesData->at(endIndex - 1), timeSeriesData->at(endIndex - 2), &g)) {
				return &g;
			} 
			return NULL;
		}
		else {
			// check if we are in a gesture (i.e. last two timestamps are above the threshold for the first three fingers)
			int endIndex = timeSeriesData->end() - timeSeriesData->begin();
			printf("Fingers 1,2,3 are %4.2f, %4.2f, %4.2f\n", timeSeriesData->at(endIndex - 1).finger_sensors[0], 
				timeSeriesData->at(endIndex - 1).finger_sensors[1], timeSeriesData->at(endIndex - 1).finger_sensors[2]);
			printf("accelerometer is %4.2f, %4.2f, %4.2f\n", timeSeriesData->at(endIndex - 1).accelerometer[0],
				timeSeriesData->at(endIndex - 1).accelerometer[1], timeSeriesData->at(endIndex - 1).accelerometer[2]);
			printf("gyroscope is %4.2f, %4.2f, %4.2f\n", timeSeriesData->at(endIndex - 1).gyroscope[0],
				timeSeriesData->at(endIndex - 1).gyroscope[1], timeSeriesData->at(endIndex - 1).gyroscope[2]);
			if (fingersInFist(timeSeriesData->at(endIndex - 1)) && fingersInFist(timeSeriesData->at(endIndex - 2))) {
				printf("Fingers in fist\n");
				inGesture = true;
			}
		}
	}

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
				elt.gyroscope[0], elt.gyroscope[1], elt.gyroscope[2]);
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
				elt.gyroscope[0], elt.gyroscope[1], elt.gyroscope[2]);

			// now write it to the file
			outfile.write(sensorInfoString, n);
		}
		recordingOnLock.unlock();
	}
}

/*
* Helper function to see if finger sensors are above threshold
*/
bool fingersInFist(SensorInfo elt) {
	return (elt.finger_sensors[0] >= fingerOneThresh && elt.finger_sensors[1] >= fingerTwoThresh && elt.finger_sensors[2] >= fingerThreeThresh);
}

/*
* Helper Function to see if we are no longer in the fist gesture position for two straight timestamps
* This is determined by two fingers being below the threshold
*/
bool outOfFist(SensorInfo eltA, SensorInfo eltB) {
	bool fingerOne = eltA.finger_sensors[0] < fingerOneThresh && eltB.finger_sensors[0] < fingerOneThresh;
	bool fingerTwo = eltA.finger_sensors[1] < fingerTwoThresh && eltB.finger_sensors[1] < fingerTwoThresh;
	bool fingerThree = eltA.finger_sensors[2] < fingerThreeThresh && eltB.finger_sensors[2] < fingerThreeThresh;
	return (fingerOne && fingerTwo || fingerOne && fingerThree || fingerTwo && fingerThree); // if two fingers are out of the fist for two straight sensor infos, we return true
}

/*
* Helper function that contains the makeshift decision tree for deciding if two timestamps are characteristic of a known gesture
* Possible gestures are Pan Up, Pan Down, Pan Left, Pan Right, Rotation Clockwise, Rotation Counterclockwise
* This function modifies the gesture object passed in and returns true if gesture was made
*
* Important Note: All gestures must start with a fist, with the palm facing down so the accelerometer has the expected vector measurement.
*/
bool decideGesture(SensorInfo eltA, SensorInfo eltB, Gesture *gesture) {
	// second pass on decision tree
	// decide whether we are in possible rotate or pan first using gyroscope
	if (gyroscopeStable("any", eltA, eltB)) {
		if (accelerometerStable(eltA, eltB)) {
			// no gesture
			return false;
		}
		else {
			// might be a pan - pan can be on z axis (up or down), or y axis (left or right)
			// check Z axis first
			if (inPan("z", eltA, eltB)) {
				// pan gesture made - at least z axis
				gesture->gestureCode = PAN;
				gesture->x = 0;
				gesture->z = (eltA.accelerometer[2] > accelerometerUpperThreshZ || eltB.accelerometer[2] > accelerometerUpperThreshZ) ? 1 : -1;
				if (inPan("y", eltA, eltB)) {
					// pan on y axis too
					gesture->y = (eltA.accelerometer[1] > accelerometerUpperThreshY || eltB.accelerometer[1] > accelerometerUpperThreshY) ? 1 : -1;
					return true;
				}
				else {
					// just a pan on z axis
					gesture->y = 0;
					return true;
				}
			}
			else {
				if (inPan("y", eltA, eltB)) {
					// pan gesture in y direction
					gesture->gestureCode = PAN;
					gesture->x = 0;
					gesture->y = (eltA.accelerometer[1] > accelerometerUpperThreshY || eltB.accelerometer[1] > accelerometerUpperThreshY) ? 1 : -1;
					gesture->z = 0;
					return true;
				}
				else {
					// no gesture
					return false;
				}
			}
		}
	}
	else {
		// we might have a rotate
		if (gyroscopeStable("x", eltA, eltB)) {
			if (gyroscopeStable("y", eltA, eltB)) {
				if (gyroscopeStable("z", eltA, eltB)) {
					// all stable - code shouldnt get here, but this means we dont have a gesture
					return false;
				}
				else {
					// unstable axis is z (3rd gyroscope axis)
					gesture->gestureCode = ROTATE;
					gesture->x = 0;
					gesture->y = 0;
					gesture->z = sign(eltA.gyroscope[2]);
					return true;
				}
			}
			else {
				// at least y is unstable
				gesture->gestureCode = ROTATE;
				gesture->x = 0;
				gesture->y = sign(eltA.gyroscope[1]);
				if (gyroscopeStable("z", eltA, eltB)) {
					// just y is unstable
					gesture->z = 0;
					return true;
				}
				else {
					// unstable axis is also z (3rd gyroscope axis)
					gesture->z = sign(eltA.gyroscope[2]);
					return true;
				}
			}
		}
		else {
			// at least x is unstable (we have a rotate gesture)
			gesture->gestureCode = ROTATE;
			gesture->x = sign(eltA.gyroscope[0]);
			if (gyroscopeStable("y", eltA, eltB)) {
				// y is stable
				gesture->y = 0;
				if (gyroscopeStable("z", eltA, eltB)) {
					gesture->z = 0;
					return true;
				}
				else {
					// z is also unstable
					gesture->z = sign(eltA.gyroscope[2]);
					return true;
				}
			}
			else {
				// y is also unstable
				gesture->y = sign(eltA.gyroscope[1]);
				if (gyroscopeStable("z", eltA, eltB)) {
					gesture->z = 0;
					return true;
				}
				else {
					// all three axes are unstable
					gesture->z = sign(eltA.gyroscope[2]);
					return true;
				}
			}
		}
	}



	/* first pass on decision tree - this just goes down to the right
	// decide rotates first
	// rotate clockwise puts gyroscope x > 250, and the other two axes should be less than 100
	if (eltA.gyroscope[0] > 250 &&  eltB.gyroscope[0] > 250 
		&& abs(eltA.gyroscope[1]) < 100 && abs(eltA.gyroscope[2]) < 100
		&& abs(eltB.gyroscope[1]) < 100 && abs(eltB.gyroscope[2]) < 100)
	{
		gesture->gestureCode = ROTATE;
		gesture->x = 0;
		gesture->y = -1;
		gesture->z = -1;
		return true;
	}
	// rotate ctrclockwise puts gyroscope x < -250, and the other two axes stay under abs(150)
	if (eltA.gyroscope[0] < -250 && eltB.gyroscope[0] < -250
		&& abs(eltA.gyroscope[1]) < 150 && abs(eltA.gyroscope[2]) < 150
		&& abs(eltB.gyroscope[1]) < 150 && abs(eltB.gyroscope[2]) < 150)
	{
		gesture->gestureCode = ROTATE;
		gesture->x = 0;
		gesture->y = 1;
		gesture->z = -1;
		return true;
	}

	// then check pans

	// Pan down: either we see a significant jump from 1 down in the z for the accelerometer, or we see two consecutive readings with downward acceleration
	if ((eltA.accelerometer[2] < 1 && (eltB.accelerometer[2] - eltA.accelerometer[2]) > 0.3) 
		|| (eltB.accelerometer[2] < 0.4 && eltA.accelerometer[2] < 0.6)) {
		gesture->gestureCode = PAN;
		gesture->x = 0;
		gesture->y = -1;
		gesture->z = 0;
		return true;
	}

	// pan up: either we see a significant jump above 1 up for the z accelerometer, or we see two consectuvie readings above 1.25
	if ((eltA.accelerometer[2] > 1 && (eltA.accelerometer[2] - eltB.accelerometer[2] > 0.4))
		|| (eltB.accelerometer[2] > 1.25 && eltA.accelerometer[2] > 1.25)) 
	{
		gesture->gestureCode = PAN;
		gesture->x = 0;
		gesture->y = 1;
		gesture->z = 0;
		return true;
	}

	// pan left: y should be close to -0.5 to start, then move positive by at least .4
	if ((closeTo(eltB.accelerometer[1], -0.5) && (eltB.accelerometer[1] - eltA.accelerometer[1] > 0.4))) {
		gesture->gestureCode = PAN;
		gesture->x = -1;
		gesture->y = 0;
		gesture->z = 0;
		return true;
	}

	// pan right: y gets close to -1
	if ((closeTo(eltB.accelerometer[1], -1)) || (closeTo(eltA.accelerometer[1], -1)) || (eltA.accelerometer[1] < -1) || (eltB.accelerometer[1] < -1)) {
		gesture->gestureCode = PAN;
		gesture->x = 1;
		gesture->y = 0;
		gesture->z = 0;
		return true;
	}*/
	
	// nothing registered, so return false
	return false;
}

bool closeTo(float a, float b) {
	return (abs(a - b) < 0.2);
}

int sign(int x) {
	if (x < 0) return -1;
	return 1;
}

bool gyroscopeStable(const char* axes, SensorInfo eltA, SensorInfo eltB) {
	if (strcmp(axes, "any") == 0) {
		return abs(eltA.gyroscope[0]) < stableAxis
			&& abs(eltA.gyroscope[1]) < stableAxis
			&& abs(eltA.gyroscope[2]) < stableAxis
			&& abs(eltB.gyroscope[0]) < stableAxis
			&& abs(eltB.gyroscope[1]) < stableAxis
			&& abs(eltB.gyroscope[2]) < stableAxis;
	}
	else if (strcmp(axes, "x")) {
		return abs(eltA.gyroscope[0]) < stableAxis
			&& abs(eltB.gyroscope[0]) < stableAxis;
	}
	else if (strcmp(axes, "y")) {
		return abs(eltA.gyroscope[1]) < stableAxis
			&& abs(eltB.gyroscope[1]) < stableAxis;
	}
	else if (strcmp(axes, "z")) {
		return abs(eltA.gyroscope[2]) < stableAxis
			&& abs(eltB.gyroscope[2]) < stableAxis;
	}
	else {
		return true;
	}
}

// returns true if accelerometer is in unit vector position (0,0,1)
bool accelerometerStable(SensorInfo eltA, SensorInfo eltB) {
	return (closeTo(eltA.accelerometer[0], 0) && closeTo(eltA.accelerometer[1], 0) && closeTo(eltA.accelerometer[2], 1)
		&& closeTo(eltB.accelerometer[0], 0) && closeTo(eltB.accelerometer[1], 0) && closeTo(eltB.accelerometer[2], 1));
}

// returns true if in pan for axis provided
// axis can be Z or Y
bool inPan(const char* axis, SensorInfo eltA, SensorInfo eltB) {
	if (strcmp(axis, "z") == 0) {
		if (eltA.accelerometer[2] > accelerometerUpperThreshZ || eltB.accelerometer[2] > accelerometerUpperThreshZ) {
			return true;
		}
		else if (eltA.accelerometer[2] < accelerometerLowerThreshZ || eltB.accelerometer[2] < accelerometerLowerThreshZ) {
			return true;
		}
		else {
			return false;
		}
	}
	else if (strcmp(axis, "y") == 0) {
		if (eltA.accelerometer[1] > accelerometerUpperThreshY || eltB.accelerometer[1] > accelerometerUpperThreshY) {
			return true;
		}
		else if (eltA.accelerometer[1] < accelerometerLowerThreshY || eltB.accelerometer[1] < accelerometerLowerThreshY) {
			return true;
		}
		else {
			return false;
		}
	}
	else {
		return false;
	}
}


