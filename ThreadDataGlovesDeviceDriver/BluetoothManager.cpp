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
#include <string>
#include <rpcdce.h>

using namespace Platform;
using namespace Windows::Devices;

concurrency::task<void> connectToGlove(unsigned long long bluetoothAddress, listenCallback c);
SensorInfo newSensorInfo(float x, float y, float z);

GUID serviceUUID;
GUID gyroXCharGUID;
GUID gyroYCharGUID;
GUID gyroZCharGUID;

BluetoothManager::BluetoothManager(bool mock) {
	mock = mock;
	connected = false;
}

BluetoothManager::~BluetoothManager() {
	// no-op
}

void BluetoothManager::findGlove(listenCallback c) {
	printf("In find glove \n");
	fflush(stdout);
	
	Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ bleAdvertisementWatcher = ref new Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher();
	bleAdvertisementWatcher->ScanningMode = Bluetooth::Advertisement::BluetoothLEScanningMode::Active;
	bleAdvertisementWatcher->Received += ref new Windows::Foundation::TypedEventHandler<Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^, Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^>(
		[bleAdvertisementWatcher, c](Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher^ watcher, Bluetooth::Advertisement::BluetoothLEAdvertisementReceivedEventArgs^ eventArgs) {
			auto serviceUuids = eventArgs->Advertisement->ServiceUuids;
			String^ localName = eventArgs->Advertisement->LocalName;
			if (wcscmp(localName->Begin(), L"Salmon Glove") == 0) {
				printf("Salmon glove found! %llu\n", eventArgs->BluetoothAddress);
				bleAdvertisementWatcher->Stop();
				connectToGlove(eventArgs->BluetoothAddress, c);
			}
		});
	bleAdvertisementWatcher->Start();
}

concurrency::task<void> connectToGlove(unsigned long long bluetoothAddress, listenCallback c) {
	CLSIDFromString(L"{1b9b0000-3e7e-4c78-93b3-0f86540298f1}", &serviceUUID); // this is the service UUID for the salmon glove
	CLSIDFromString(L"{1b9b0004-3e7e-4c78-93b3-0f86540298f1}", &gyroXCharGUID);
	CLSIDFromString(L"{1b9b0005-3e7e-4c78-93b3-0f86540298f1}", &gyroYCharGUID);
	CLSIDFromString(L"{1b9b0006-3e7e-4c78-93b3-0f86540298f1}", &gyroZCharGUID);

	auto device = co_await Bluetooth::BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress);
	printf("Device found \n");
	auto allServices = co_await device->GetGattServicesAsync();
	for (int i = 0; i < allServices->Services->Size; i++) {
		OLECHAR* guidString;
		StringFromCLSID(allServices->Services->GetAt(i)->Uuid, &guidString);
		std::cout << "Service found " << guidString << std::endl;
	}
	printf("Services found \n");
	auto servicesResult = co_await device->GetGattServicesForUuidAsync(serviceUUID);
	auto service = servicesResult->Services->GetAt(0);
	printf("Service found \n");
	auto gyroXCharResult = (co_await service->GetCharacteristicsForUuidAsync(gyroXCharGUID))->Characteristics->GetAt(0);
	auto gyroYCharResult = (co_await service->GetCharacteristicsForUuidAsync(gyroYCharGUID))->Characteristics->GetAt(0);
	auto gyroZCharResult = (co_await service->GetCharacteristicsForUuidAsync(gyroZCharGUID))->Characteristics->GetAt(0);
	printf("Characteristics found \n");

	// infinite loop where we are reading
	for (;;) {
		Sleep(1000);
		auto x = (co_await gyroXCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
		auto y = (co_await gyroYCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;
		auto z = (co_await gyroZCharResult->ReadValueAsync(Bluetooth::BluetoothCacheMode::Uncached))->Value;

		// get each raw value
		auto dataReaderX = Windows::Storage::Streams::DataReader::FromBuffer(x);
		float xVal = dataReaderX->ReadSingle();
		auto dataReaderY = Windows::Storage::Streams::DataReader::FromBuffer(y);
		float yVal = dataReaderY->ReadSingle();
		auto dataReaderZ = Windows::Storage::Streams::DataReader::FromBuffer(z);
		float zVal = dataReaderZ->ReadSingle();

		printf("Gyroscope values are  %2.2f, %2.2f, %2.2f", xVal, yVal, zVal);

		// call callback listener
		c(newSensorInfo(xVal, yVal, zVal));
	}
}

// to form sensor info object from just gyro x,y,z values
SensorInfo newSensorInfo(float x, float y, float z) {
	SensorInfo i = { {0, 0,0,0,0}, {0,0,0}, {x,y,z} };
	return i;
}