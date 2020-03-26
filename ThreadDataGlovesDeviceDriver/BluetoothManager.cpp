#include "BluetoothManager.h"
#include "stdafx.h"
#include <iostream>
#include <Windows.Foundation.h>
#include <Windows.Devices.Bluetooth.h>
#include <Windows.Devices.Bluetooth.Advertisement.h>
#include <wrl/wrappers/corewrappers.h>
#include <wrl/event.h>
#include <collection.h>
#include <ppltasks.h>
#include <string>
#include <sstream> 
#include <iomanip>
#include <experimental/resumable>
#include <pplawait.h>
#include <cstring>
#include <rpcdce.h>

using namespace Platform;
using namespace Windows::Devices;

concurrency::task<void> connectToGlove(unsigned long long bluetoothAddress, listenCallback c, 
	errorCallback e, GestureRecognizer **recognizer, bool *globalGloveFound, CalibrationStruct *globalCalibrationStruct, 
	std::mutex* calLock, std::mutex* waitLock);
SensorInfo newSensorInfo(float accelerometerValues[3], float magnometerValues[3], uint16_t threadValues[5], CalibrationInfo ci);
CalibrationInfo newCalibrationInfo(uint16_t minReading[5], uint16_t maxReading[5]);

// GUID for the services and characteristics
GUID serviceUUID;
GUID dataCharGUID;

// instantiate the locks
std::mutex BluetoothManager::calibrationLock;
std::mutex BluetoothManager::waitingForCalibrationLock;

BluetoothManager::BluetoothManager(HANDLE *heapPtr) {
	connected = false; // this isn't used for anything currently
	recognizer = new GestureRecognizer(heapPtr);
}

BluetoothManager::~BluetoothManager() {
	// no-op
}

// TEMPORARY CALLS FOR GESTURES
bool BluetoothManager::startRecording() {
	return recognizer->startRecording();
}

bool BluetoothManager::endRecording(char *filepath) {
	return recognizer->endRecording(filepath);
}

/*
 * findGlove called to start the process of connecting with the glove over bluetooth
 * There is no timeout, so this will go until it finds the glove
 * Takes in the listenCallback used once we are connected + receiving gestures
 */
void BluetoothManager::findGlove(listenCallback c, errorCallback e, bool *globalGloveFound, CalibrationStruct* globalCalibrationStruct) {
	printf("In find glove \n");
	fflush(stdout);
	
	Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ bleAdvertisementWatcher = ref new Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher();
	bleAdvertisementWatcher->ScanningMode = Bluetooth::Advertisement::BluetoothLEScanningMode::Active;
	// Adds an event handler for when we find a new bluetooth device. If its local name is Salmon Glove, then we attempt to connect to it.
	bleAdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^, Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
		[bleAdvertisementWatcher, c, e, this, globalGloveFound, globalCalibrationStruct](Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ watcher, Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ eventArgs) {
			auto serviceUuids = eventArgs->Advertisement->ServiceUuids;
			String^ localName = eventArgs->Advertisement->LocalName;
			if (wcscmp(localName->Begin(), L"Salmon Glove") == 0) {
				printf("Salmon glove found! %llu\n", eventArgs->BluetoothAddress);
				bleAdvertisementWatcher->Stop();
				connectToGlove(eventArgs->BluetoothAddress, c, e, &recognizer, globalGloveFound, globalCalibrationStruct, &calibrationLock, &waitingForCalibrationLock);
			}
		});
	bleAdvertisementWatcher->Start(); // this starts the async process above
}

concurrency::task<void> connectToGlove(unsigned long long bluetoothAddress, listenCallback c, 
	errorCallback e, GestureRecognizer **recognizer, bool *globalGloveFound, CalibrationStruct *globalCalibrationStruct, 
	std::mutex *calLock, std::mutex *waitLock) {

	auto _ = CLSIDFromString(L"{1b9b0000-3e7e-4c78-93b3-0f86540298f1}", &serviceUUID); // this is the service UUID for the salmon glove
	_ = CLSIDFromString(L"{1b9b0001-3e7e-4c78-93b3-0f86540298f1}", &dataCharGUID);


	auto device = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	printf("Device found \n");
	auto allServices = co_await device->GetGattServicesAsync();
	if (allServices->Services->Size == 0) {
		// error getting services, so we call the error callback
		e();
	}
	else {
		for (unsigned i = 0; i < allServices->Services->Size; i++) {
			OLECHAR* guidString;
			StringFromCLSID(allServices->Services->GetAt(i)->Uuid, &guidString);
			std::cout << "Service found " << guidString << std::endl;
		}

		Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic^ dataCharResult;
		try {
			printf("Services found \n");
			auto servicesResult = co_await device->GetGattServicesForUuidAsync(serviceUUID);
			auto service = servicesResult->Services->GetAt(0);
			printf("Service found \n");
			dataCharResult = (co_await service->GetCharacteristicsForUuidAsync(dataCharGUID))->Characteristics->GetAt(0);
			printf("Characteristics found \n");
		}
		catch (OutOfBoundsException^) {
			// Error getting the characteristics or services
			delete device;
			(*recognizer)->zeroSavedCalibration();
			e();
			return;
		}

		// We are connected at this point
		*globalGloveFound = true;
		// infinite loop where we are reading
		long timeCount = 0;
		bool doingCalibration = false;
		uint16_t minValues[5] = { 0 };
		uint16_t maxValues[5] = { 0 };

		for (;;) {
			// detect if bluetooth device disconnected
			if (device->ConnectionStatus == Bluetooth::BluetoothConnectionStatus::Disconnected) {
				//device->Close();
				printf("Device not connected\n");
				(*recognizer)->zeroSavedCalibration();
				e();
				break;
			}

			Windows::Storage::Streams::IBuffer^ data;
			try {
				data = (co_await dataCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
			}
			catch (Exception^) { 
				// if device disconnected while reading, we catch that exception here
				delete device;
				(*recognizer)->zeroSavedCalibration();
				e();
				break;
			}

			// get each raw value
			auto dataReader = Windows::Storage::Streams::DataReader::FromBuffer(data);
			byte gloveData[34];
			for (int i = 0; i < 34; i++) {
				gloveData[i] = dataReader->ReadByte();
				//printf("%u ||", gloveData[i]);
			}
			//printf("\n");
			float axVal = *(((float*)gloveData));
			float ayVal = *(((float*)gloveData) + 1);
			float azVal = *(((float*)gloveData) + 2);
			float mxVal = *(((float*)gloveData) + 3);
			float myVal = *(((float*)gloveData) + 4);
			float mzVal = *(((float*)gloveData) + 5);
			uint16_t thread1 = *(((uint16_t*)gloveData) + 12);
			uint16_t thread2 = *(((uint16_t*)gloveData) + 13);
			uint16_t thread3 = *(((uint16_t*)gloveData) + 14);
			uint16_t thread4 = *(((uint16_t*)gloveData) + 15);
			uint16_t thread5 = *(((uint16_t*)gloveData) + 16);


			//printf("Accelerometer values are  %2.2f, %2.2f, %2.2f\n", axVal, ayVal, azVal);
			//printf("Magnetometer values are %3.3f, %3.3f, %3.3f\n", mxVal, myVal, mzVal);
			//printf("Thread values are %u, %u, %u, %u, %u\n", thread1, thread2, thread3, thread4, thread5);

			// add sensor data to time series, if we are at a certain stride of time, ask gesture 
			// recognizer to try to find a gesture
			float accelerometerValues[3] = { axVal, ayVal, azVal };
			float gyroscopeValues[3] = { mxVal, myVal,  mzVal };
			uint16_t threadValues[5] = { thread1, thread2, thread3, thread4, thread5 };

			if ((*recognizer)->isCalibrationSet()) {
				timeCount += 1;
				if (timeCount > TIME_SERIES_SIZE) timeCount = 0;

				// scale sensor info by calibration info
				SensorInfo s = newSensorInfo(accelerometerValues, gyroscopeValues, threadValues, (*recognizer)->getCalibrationInfo());
				(*recognizer)->addToTimeSeries(s);

				if (timeCount % 2 == 0) {
					Gesture* g = (*recognizer)->recognize();
					if (g != NULL) c(g); // call callback listener
				}
			}
			else {
				// are we currently calibrating?
				if (globalCalibrationStruct->calibrationTrigger && !doingCalibration) {
					// determine wheteher or not to do a new calibration
					calLock->lock();
					globalCalibrationStruct->calibrationTrigger = false;
					calLock->unlock();
					if (globalCalibrationStruct->from_saved_file) {
						// we have it in the calibrationinfo - this will set calibration
						printf("Calibrating from saved file\n");
						(*recognizer)->setCalibrationWithData(globalCalibrationStruct->ci);
						globalCalibrationStruct->gloveCalibrated = true;
						globalCalibrationStruct->from_saved_file = false; // reset it so there is no confusion in future calibration
						timeCount = 0;
					}
					else {
						doingCalibration = true; // this will tell us to record data in future loop iterations
						waitLock->lock(); // lock this and release when we get the calibration is over trigger from the main thread
						globalCalibrationStruct->calibrationStarted = true;
						minValues[0] = thread1;
						minValues[1] = thread2;
						minValues[2] = thread3;
						minValues[3] = thread4;
						minValues[4] = thread5;
						maxValues[0] = thread1;
						maxValues[1] = thread2;
						maxValues[2] = thread3;
						maxValues[3] = thread4;
						maxValues[4] = thread5;
					}
				}
				else if (globalCalibrationStruct->calibrationTrigger && doingCalibration) {
					// this is when we stop calibrating and save the calibration data
					calLock->lock();
					waitLock->unlock(); // we have acquired calLock, so now release waitLock
					doingCalibration = false;
					globalCalibrationStruct->calibrationTrigger = false;
					globalCalibrationStruct->calibrationStarted = false;
					
					// save  min and max values
					CalibrationInfo ci = newCalibrationInfo(minValues, maxValues);
					printf("Calibration data save is for min values %u, %u, %u, %u, %u", minValues[0], minValues[1], minValues[2], minValues[3], minValues[4]);
					(*recognizer)->setCalibrationWithData(ci);
					globalCalibrationStruct->gloveCalibrated = true;
					timeCount = 0;

					// add it to the global struct so main thread can use the calibration data
					globalCalibrationStruct->ci = ci;
					calLock->unlock();
				}
				else if (doingCalibration) {
					minValues[0] = thread1 < minValues[0] ? thread1 : minValues[0];
					minValues[1] = thread2 < minValues[1] ? thread2 : minValues[1];
					minValues[2] = thread3 < minValues[2] ? thread3 : minValues[2];
					minValues[3] = thread4 < minValues[3] ? thread4 : minValues[3];
					minValues[4] = thread5 < minValues[4] ? thread5 : minValues[4];
					maxValues[0] = thread1 > maxValues[0] ? thread1 : maxValues[0];
					maxValues[1] = thread2 > maxValues[1] ? thread2 : maxValues[1];
					maxValues[2] = thread3 > maxValues[2] ? thread3 : maxValues[2];
					maxValues[3] = thread4 > maxValues[3] ? thread4 : maxValues[3];
					maxValues[4] = thread5 > maxValues[4] ? thread5 : maxValues[4];
				}
			}
		}
	}
}

// Form sensor info struct from all the accelerometer, magnometer and thread values
SensorInfo newSensorInfo(float accelerometerValues[3], float gyroscopeValues[3], uint16_t threadValues[5], CalibrationInfo ci) {
	// normalize values first
	float newThreadValues[5];
	for (int i = 0; i < 5; i++) {
		// need to check for divide by zero - when calibration is zero for whatever reason
		if (ci.maxReading[i] - ci.minReading[i] == 0) {
			newThreadValues[i] = (float)threadValues[i];
		}
		else {
			newThreadValues[i] = ((float)threadValues[i] - (float)ci.minReading[i]) / ((float)ci.maxReading[i] - (float)ci.minReading[i]);
		}
	}

	//printf("Normalized thread values are %4.2f, %4.2f, %4.2f, %4.2f, %4.2f\n", newThreadValues[0], newThreadValues[1], newThreadValues[2], newThreadValues[3], newThreadValues[4]);

	SensorInfo SI = { {newThreadValues[0], newThreadValues[1], newThreadValues[2], newThreadValues[3], newThreadValues[4]},
					{accelerometerValues[0], accelerometerValues[1], accelerometerValues[2]},
					{gyroscopeValues[0], gyroscopeValues[1], gyroscopeValues[2]} };
	return SI;
}

// Returns calibration info struct from min reading and max reading arrays
CalibrationInfo newCalibrationInfo(uint16_t minReading[5], uint16_t maxReading[5]) {
	CalibrationInfo ci;
	ci.maxReading[0] = maxReading[0];
	ci.maxReading[1] = maxReading[1];
	ci.maxReading[2] = maxReading[2];
	ci.maxReading[3] = maxReading[3];
	ci.maxReading[4] = maxReading[4];
	ci.minReading[0] = minReading[0];
	ci.minReading[1] = minReading[1];
	ci.minReading[2] = minReading[2];
	ci.minReading[3] = minReading[3];
	ci.minReading[4] = minReading[4];
	return ci;
}

