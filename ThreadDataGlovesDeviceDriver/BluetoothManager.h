#include "GestureRecognizer.h"
#include <mutex>
#define MAX_FILEPATH_LENGTH 100

typedef void (*listenCallback)(Gesture *arg);
typedef void (*errorCallback)();

// This holds everything we need to communicate with the background thread for starting/doing calibration
struct CalibrationStruct {
	char savedFilePath[MAX_FILEPATH_LENGTH];
	bool from_saved_file; // if it is from a saved file then, the saved file path variable will have the file name in it to load from
	bool calibrationTrigger; // used as an indicator between threads to tell the background thread to start recording data, and when to stop
	bool gloveCalibrated;
	bool calibrationStarted;
	CalibrationInfo ci;
} typedef CalibrationStruct;

class BluetoothManager
{
public:
	static std::mutex calibrationLock;

	BluetoothManager(HANDLE *heapPtr);
	~BluetoothManager();
	void findGlove(listenCallback c, errorCallback e, bool *globalGloveFound, CalibrationStruct *globalCalibrationStruct);
	
	// Temporary calls for building out our gesture recognizer
	bool startRecording();
	bool endRecording(char *filepath);

private:
	bool connected;
	GestureRecognizer *recognizer;
};

