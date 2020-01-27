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
	errorCallback e, GestureRecognizer **recognizer, bool *globalGloveFound);
SensorInfo newSensorInfo(float x, float y, float z);

// GUID for the services and characteristics
GUID serviceUUID;
GUID dataCharGUID;

BluetoothManager::BluetoothManager(HANDLE *heapPtr) {
	connected = false; // this isn't used for anything currently
	recognizer = new GestureRecognizer(heapPtr);
}

BluetoothManager::~BluetoothManager() {
	// no-op
}

/*
 * findGlove called to start the process of connecting with the glove over bluetooth
 * There is no timeout, so this will go until it finds the glove
 * Takes in the listenCallback used once we are connected + receiving gestures
 */
void BluetoothManager::findGlove(listenCallback c, errorCallback e, bool *globalGloveFound) {
	printf("In find glove \n");
	fflush(stdout);
	
	Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ bleAdvertisementWatcher = ref new Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher();
	bleAdvertisementWatcher->ScanningMode = Bluetooth::Advertisement::BluetoothLEScanningMode::Active;
	// Adds an event handler for when we find a new bluetooth device. If its local name is Salmon Glove, then we attempt to connect to it.
	bleAdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^, Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
		[bleAdvertisementWatcher, c, e, this, globalGloveFound](Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ watcher, Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ eventArgs) {
			auto serviceUuids = eventArgs->Advertisement->ServiceUuids;
			String^ localName = eventArgs->Advertisement->LocalName;
			if (wcscmp(localName->Begin(), L"Salmon Glove") == 0) {
				printf("Salmon glove found! %llu\n", eventArgs->BluetoothAddress);
				bleAdvertisementWatcher->Stop();
				connectToGlove(eventArgs->BluetoothAddress, c, e, &recognizer, globalGloveFound);
			}
		});
	bleAdvertisementWatcher->Start(); // this starts the async process above
}

concurrency::task<void> connectToGlove(unsigned long long bluetoothAddress, listenCallback c, 
	errorCallback e, GestureRecognizer **recognizer, bool *globalGloveFound) {

	CLSIDFromString(L"{1b9b0000-3e7e-4c78-93b3-0f86540298f1}", &serviceUUID); // this is the service UUID for the salmon glove
	CLSIDFromString(L"{1b9b0001-3e7e-4c78-93b3-0f86540298f1}", &dataCharGUID);


	auto device = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	printf("Device found \n");
	auto allServices = co_await device->GetGattServicesAsync();
	if (allServices->Services->Size == 0) {
		// error getting services, so we call the error callback
		e();
	}
	else {
		for (int i = 0; i < allServices->Services->Size; i++) {
			OLECHAR* guidString;
			StringFromCLSID(allServices->Services->GetAt(i)->Uuid, &guidString);
			std::cout << "Service found " << guidString << std::endl;
		}

		Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic^ dataCharResult;
		//Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic^ gyroYCharResult;
		//Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic^ gyroZCharResult;
		try {
			printf("Services found \n");
			auto servicesResult = co_await device->GetGattServicesForUuidAsync(serviceUUID);
			auto service = servicesResult->Services->GetAt(0);
			printf("Service found \n");
			dataCharResult = (co_await service->GetCharacteristicsForUuidAsync(dataCharGUID))->Characteristics->GetAt(0);
			//gyroYCharResult = (co_await service->GetCharacteristicsForUuidAsync(gyroYCharGUID))->Characteristics->GetAt(0);
			//gyroZCharResult = (co_await service->GetCharacteristicsForUuidAsync(gyroZCharGUID))->Characteristics->GetAt(0);
			printf("Characteristics found \n");
		}
		catch (OutOfBoundsException^) {
			// Error getting the characteristics or services
			e();
			return;
		}

		// We are connected at this point
		*globalGloveFound = true;
		// infinite loop where we are reading
		long timeCount = 0;
		for (;;) {
			// TODO: change sleep or remove it
			timeCount += 1;
			if (timeCount > 100 * TIME_SERIES_SIZE) timeCount = 0;
			
			// detect if bluetooth device disconnected
			if (device->ConnectionStatus == Bluetooth::BluetoothConnectionStatus::Disconnected) {
				//device->Close();
				printf("Device not connected\n");
				e();
				break;
			}

			Windows::Storage::Streams::IBuffer^ data;
			//Windows::Storage::Streams::IBuffer^ y;
			//Windows::Storage::Streams::IBuffer^ z;
			try {
				data = (co_await dataCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
				//y = (co_await gyroYCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
				//z = (co_await gyroZCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
			}
			catch (Exception^) { 
				// if device disconnected while reading, we catch that exception here
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

			//auto dataReaderY = Windows::Storage::Streams::DataReader::FromBuffer(y);
			//float yVal = dataReaderY->ReadSingle();
			//auto dataReaderZ = Windows::Storage::Streams::DataReader::FromBuffer(z);
			//float zVal = dataReaderZ->ReadSingle();

			printf("Accelerometer values are  %2.2f, %2.2f, %2.2f\n", axVal, ayVal, azVal);
			printf("Magnetometer values are %3.3f, %3.3f, %3.3f\n", mxVal, myVal, mzVal);
			printf("Thread values are %u, %u, %u, %u, %u\n", thread1, thread2, thread3, thread4, thread5);

			// add sensor data to time series, if we are at a certain stride of time, ask gesture 
			// recognizer to try to find a gesture
			SensorInfo s = newSensorInfo(xVal, yVal, zVal);
			(*recognizer)->addToTimeSeries(s);

			if (timeCount % 1000 == 0) {
				Gesture *g = (*recognizer)->recognize();
				if (g != NULL) c(g); // call callback listener
			}
		}
	}
}

// to form sensor info object from just gyro x,y,z values
SensorInfo newSensorInfo(float x, float y, float z) {
	SensorInfo i = { {0, 0,0,0,0}, {0,0,0}, {x,y,z} };
	return i;
}