// ThreadDataGlovesDeviceDriver.cpp : This file contains the 'main' function. Program execution begins and ends there.

#ifndef WIN32
	#define WIN32
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "BluetoothManager.h"
#include <iostream>
#include <fstream>
#include <future>
#include <thread>
#include <vector>
#include <string>
#include <atlstr.h>
#include <codecvt>
#include <locale>
#include <assert.h>
#include <wrl/wrappers/corewrappers.h>
#include <wrl/event.h>

#define PORT 10500 // This is the port number used for socket communication with the device driver
#define BUFFER_SIZE 1000
#define FD_SETSIZE 1024

using namespace std; 

const int ERROR_RET = -1;
const int SUCCESS_RET = 1;
const int ASCII_NUM_VAL = 48;
const wstring PIPE_DIR = L"\\\\.\\PIPE\\";
const string S_PIPE_DIR = "\\\\.\\PIPE\\";
bool RETRY_FIND_GLOVE = false; // used to try to reconnect to glove

struct ProcListener {
	const char *namedPipePath;
	HANDLE namedPipe;
	const char *procName;
	SOCKET socket;
} typedef ProcListener;

struct BufferHolder {
	char* buf;
	int size;
} typedef BufferHolder;

// Request codes sent by applications to the device driver over the socket
enum RequestCodes {
	HI=1, BYE, BATTERY_LIFE, START_CALIBRATION, END_CALIBRATION, USE_SAVED_CALIBRATION_DATA, IS_CALIBRATED, IS_GLOVE_CONNECTED, START_RECORDING, END_RECORDING
};

// Return codes used by device driver when sending info back to clients
enum ReturnCodes {
	FAILURE=1, SUCCESS=2
};

/******  Function Declarations  *******/
void gestureListener(Gesture *g);
int processRequest(SOCKET i, char *requestBytes, BluetoothManager *b);
bool newNamedPipe(string processName,  ProcListener *pl);
int sendCodeResponse(SOCKET i, char code, const char *response);
void calibrate(BluetoothManager* b, CalibrationInfo &result);
void freeProcListener(SOCKET i);
void bluetoothErrorHandler();
void zeroCalibrationStruct();
bool getCalibrationInfoFromFile(char* filepath, CalibrationInfo* ci);
char* createCalibrationPayload(CalibrationInfo ci);

// global variable which is linked list of structs that contain process name & named pipes used for gesture listening
vector<ProcListener> listeners;

// global heap
HANDLE heap;

// global glove found
bool gloveFound = false;
// global glove calibrated structure
CalibrationStruct globalCalibrationStruct;
// calibration lock - used to indicate when calibration is done
mutex calibrationLock;

int main(int argc, char *argv[])
{
	INT Ret;
	WSADATA wsaData;
	SOCKET ListenSocket;
	SOCKET AcceptSocket;
	SOCKADDR_IN InternetAddr;
	FD_SET ActiveFdSet;
	FD_SET ReadFdSet;

	// set up heap
	heap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);

	// check to make sure we can do network communication
	if ((Ret = WSAStartup(0x0202, &wsaData)) != 0)
	{
		printf("WSAStartup() failed with error %d\n", Ret);
		WSACleanup();
		return 1;
	}

	// initialize security needed for bluetooth
	Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);

	auto _ = CoInitializeSecurity(
		nullptr, // TODO: "O:BAG:BAD:(A;;0x7;;;PS)(A;;0x3;;;SY)(A;;0x7;;;BA)(A;;0x3;;;AC)(A;;0x3;;;LS)(A;;0x3;;;NS)"
		-1,
		nullptr,
		nullptr,
		RPC_C_AUTHN_LEVEL_DEFAULT,
		RPC_C_IMP_LEVEL_IDENTIFY,
		NULL,
		EOAC_NONE,
		nullptr);

	// initialize bluetooth manager
	BluetoothManager bluetoothMngr(&heap);

	// zero calibration struct to make sure we have a fresh structure to start out with
	zeroCalibrationStruct();

	// start find glove, pass in listener. When listener called, we are connected with the glove.
	bluetoothMngr.findGlove((listenCallback)gestureListener, (errorCallback)bluetoothErrorHandler, &gloveFound, &globalCalibrationStruct);
	
	// socket server code for interprocess communication
	// open socket, accept connections, select loop to handle both new connections and requests for communication
	// on data request (this is endpoint), call async function and return appropriate data if necessary
	// Prepare a socket to listen for connections

	if ((ListenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		printf("WSASocket() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	InternetAddr.sin_family = AF_INET;
	InternetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	InternetAddr.sin_port = htons(PORT);

	if (::bind(ListenSocket, (PSOCKADDR)&InternetAddr, sizeof(InternetAddr)) == SOCKET_ERROR)
	{
		printf("bind() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	if (listen(ListenSocket, 5))
	{
		printf("listen() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	printf("Listening on port 10500 \n");
	printf("Everything set up for socket communication \n");

	FD_ZERO(&ActiveFdSet);
	printf("Listen socket is %u", ListenSocket);
	FD_SET(ListenSocket, &ActiveFdSet);

	// timeout
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;

	// initialize read buffer array
	BufferHolder* buffers = (BufferHolder*)HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(BufferHolder) * FD_SETSIZE);
	for (int i = 0; i < FD_SETSIZE; i++) {
		buffers[i].buf = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, BUFFER_SIZE * 2);
	}
	
	int ret;
	char buffer[BUFFER_SIZE + 1];
	while (true) {
		ReadFdSet = ActiveFdSet;
		ret = select(FD_SETSIZE, &ReadFdSet, NULL, NULL, &tv);
		printf("in select loop\n");
		if (ret < 0) {
			// select failed
			printf("select failed");
			perror("select");
		}
		else if (ret == 0) {
			printf("Timeout occured\n");
			// check if retry find glove was changed to true
			if (RETRY_FIND_GLOVE) {
				bluetoothMngr.findGlove((listenCallback)gestureListener, (errorCallback)bluetoothErrorHandler, &gloveFound, &globalCalibrationStruct);
				RETRY_FIND_GLOVE = false;
			}
		}
		else {
			// service all input pending sockets
			for (SOCKET i=0; i < FD_SETSIZE; i++) {
				if (FD_ISSET(i, &ReadFdSet)) {
					if (i == ListenSocket) {
						// new connection request
						int size = sizeof(InternetAddr);
						AcceptSocket = accept(ListenSocket, (sockaddr*)&InternetAddr, &size);
						if (AcceptSocket == INVALID_SOCKET) {
							perror("accept");
							exit(EXIT_FAILURE);
						}
						FD_SET(AcceptSocket, &ActiveFdSet);
						printf("New connection accepted");
					}
					else {
						// handle endpoint request or closed socket
						memset(buffer, 0, BUFFER_SIZE);
						printf("we have to stuff to read on a socket\n");
						int recvRet = recv(i, buffer, BUFFER_SIZE, 0);
						printf("We received %s \n", buffer);
						if (recvRet <= 0) {
							// close connection
							printf("Closing a socket");
							closesocket(i);
							FD_CLR(i, &ActiveFdSet);
							freeProcListener(i);
						}
						else {
							// if we have the end of a request, process it, otherwise store the request
							if (buffers[i].size > 0) {
								// we have stored a partial request, so add this to it
								memcpy_s(buffers[i].buf + buffers[i].size, BUFFER_SIZE * 2 - buffers[i].size, buffer, recvRet);
								buffers[i].size += recvRet;
							}
							else {
								// store this request
								memcpy_s(buffers[i].buf, BUFFER_SIZE * 2, buffer, recvRet);
								buffers[i].size = recvRet;
							}
							// check if end of request is in this payload
							for (int j = 0; j < buffers[i].size; j++) {
								if (buffers[i].buf[j] == '\n') {
									// request found - is from char 0 to j
									char* request = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, j + 1);
									memcpy_s(request, j + 1, buffers[i].buf, j + 1);
									if (buffers[i].size > j + 1) {
										// delete request and move rest up
										char *tmp = (char *)HeapAlloc(heap, HEAP_ZERO_MEMORY, BUFFER_SIZE * 2);
										int newSize = buffers[i].size - (j + 1);
										memcpy_s(tmp, BUFFER_SIZE * 2, buffers[i].buf + j + 1, newSize);
										memset(buffers[i].buf, 0, BUFFER_SIZE * 2);
										memcpy_s(buffers[i].buf, BUFFER_SIZE * 2, tmp, newSize);
										buffers[i].size = newSize;
										HeapFree(heap, NULL, tmp);
									}
									else {
										// zero out whole buffer
										memset(buffers[i].buf, 0, BUFFER_SIZE * 2);
										buffers[i].size = 0;
									}
									if (processRequest(i, request, &bluetoothMngr) == ERROR_RET) {
										closesocket(i);
										FD_CLR(i, &ActiveFdSet);
									}
									break;
								}
							}
						}
					}
				}
			}

			// check if retry find glove was changed to true
			if (RETRY_FIND_GLOVE) {
				bluetoothMngr.findGlove((listenCallback)gestureListener, (errorCallback)bluetoothErrorHandler, &gloveFound, &globalCalibrationStruct);
				RETRY_FIND_GLOVE = false;
			}
		}
	}

	return 0;
}

/*
 * Function used as a callback for the bluetooth manager. When gestures are received, this function is called.
 * Takes in SensorInfo struct, sends gesture data over all open named pipes.
 */
void gestureListener(Gesture *g) {
	printf("Gesture received! \n");

	// make payload
	TCHAR buffer[128] = { L'0' };
	buffer[0] = (int)g->gestureCode + ASCII_NUM_VAL;
	buffer[1] = ' ';
	buffer[2] = g->x + ASCII_NUM_VAL;
	buffer[3] = ',';
	buffer[4] = g->y + ASCII_NUM_VAL;
	buffer[5] = ',';
	buffer[6] = g->z + ASCII_NUM_VAL;
	const int buf_size = 7 * 2; // since we are using wchar not char
	
	// now send over named pipes
	for (vector<ProcListener>::iterator it = listeners.begin(); it < listeners.end(); it++) {
		if ((*it).namedPipe != INVALID_HANDLE_VALUE) {
			printf("Sending gesture to named pipe: %s \n", (*it).namedPipePath);
			DWORD bytesWritten;
			printf("payload to send is %ws", buffer);
			WriteFile((*it).namedPipe, buffer, buf_size, &bytesWritten, NULL);
		}
	}
} 

/*
 * processRequest used to handle incoming messages on the open sockets with clients
 * Inputs: SOCKET i (the socket we are on), char* requestBytes (the incoming message), 
 *	BluetoothManager* b (a pointer to our bluetooth manager)
 * Returns: int (ERROR_RET or SUCCESS_RET)
 */
int processRequest(SOCKET i, char *requestBytes, BluetoothManager* b) {
	printf("in process request %s\n", requestBytes);
	uint32_t firstFourBytes;
	memcpy(&firstFourBytes, requestBytes, 4);
	printf("first two bytes are, %x", firstFourBytes);
	printf("code is %d \n", requestBytes[0]);
	switch (requestBytes[0]) {
	case HI: {
		// create new named pipe for given process and return success to client
		int UUID_length = 16;
		char* procNameFromRequest = (char *)HeapAlloc(heap, HEAP_ZERO_MEMORY, UUID_length + 1);
		memset(procNameFromRequest, 0, UUID_length + 1);
		memcpy_s(procNameFromRequest, UUID_length + 1, requestBytes + 1, UUID_length);
		string name = string(procNameFromRequest);
		ProcListener pl;
		if (!newNamedPipe(name, &pl)) {
			printf("Failed to get new named pipe \n");
			DWORD lastError = GetLastError();
			printf("Last error code is %lu \n", lastError);
			// return failure to client and disconnect
			sendCodeResponse(i, FAILURE, "");
			return ERROR_RET;
		}
		printf("size of whoel thing is %d, strlen ofnamedpipepath is %d \n", sizeof(pl), strlen(pl.namedPipePath));
		printf("Named pipe pathis %s \n", pl.namedPipePath);
		printf("New named pipe created successfully \n");
		// send success to client
		printf("Success adding new client, about to send success\n");
		if (sendCodeResponse(i, SUCCESS, pl.namedPipePath) == ERROR_RET) {
			return ERROR_RET;
		}
		else {
			// add proc listener to listenerlist, and return sucess
			printf("Success sent\n");
			pl.socket = i;
			listeners.push_back(pl);
			return SUCCESS_RET;
		}
		break;
	}
	case BYE: {
		// close named pipe for process
		freeProcListener(i);
		return ERROR_RET;
		break;
	}
	case BATTERY_LIFE: {
		// no-op
		break;
	}
	case START_CALIBRATION: {
		// If our glove is connected:
		// Lock calibration lock, and trigger the boolean value in shared calibration struct global variable and then return success
		if (gloveFound) {
			b->calibrationLock.lock();
			globalCalibrationStruct.calibrationTrigger = true;
			b->calibrationLock.unlock();
			printf("Calibration succesfully started\n");
			sendCodeResponse(i, SUCCESS, "");
		}
		else {
			sendCodeResponse(i, FAILURE, "glove not connected");
		}
		break;
	}
	case END_CALIBRATION: {
		printf("About to end calibration \n");
		// if glove is connected, and calibration has been started, lock calibration lock, trigger boolean value in shared struct
		// and then wait to regain lock and then send back calibration info
		if (gloveFound && globalCalibrationStruct.calibrationStarted) {
			b->calibrationLock.lock();
			globalCalibrationStruct.calibrationTrigger = true;
			b->calibrationLock.unlock();

			// add small wait to make sure we don't beat the read loop to get this lock
			Sleep(100);
			b->calibrationLock.lock();
			// once we have gained the lock we have the calibration info we need to send back in the global calibration struct
			char* payload = createCalibrationPayload(globalCalibrationStruct.ci);
			b->calibrationLock.unlock();
			if (payload == NULL) {
				// error in the calibration
				sendCodeResponse(i, FAILURE, "Error calibrating");
			}
			else {
				printf("Calibration set\n");
				sendCodeResponse(i, SUCCESS, payload);
				HeapFree(heap, HEAP_FREE_CHECKING_ENABLED, payload);
			}

		}
		else {
			sendCodeResponse(i, FAILURE, "Glove must be connected and calibration must have been started");
		}
		break;
	}
	case USE_SAVED_CALIBRATION_DATA: {
		printf("Trying to calibrate with saved calibration data\n");
		// read filepath from request
		char filepath[BUFFER_SIZE];
		strncpy_s(filepath, BUFFER_SIZE, requestBytes + 1, BUFFER_SIZE);
		for (int i = 0; i < strlen(filepath); i++) {
			if (filepath[i] == '\n') {
				filepath[i] = '\0'; // need to remove the newline so that is not in the filepath
			}
		}

		// now read calibration info from that file
		CalibrationInfo* ci = (CalibrationInfo*)HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(CalibrationInfo));
		bool calibrationFileRead = getCalibrationInfoFromFile(filepath, ci);
		printf("After calibration file read function, result was %d\n", calibrationFileRead);
		if (!calibrationFileRead) {
			// we got an error trying to read the file, so we won't calibrate, and we will return an error to the client
			printf("Calibration file not read correctly, sending ERROR back\n");
			sendCodeResponse(i, FAILURE, "File format not correct");
			HeapFree(heap, HEAP_FREE_CHECKING_ENABLED, ci);
			ci = NULL;
		}
		else {
			// add this to the global calibration struct, and turn the trigger on so that the background thread can use it
			b->calibrationLock.lock();
			printf("Saving calibration data, min value 3 is %u \n", ci->minReading[2]);
			globalCalibrationStruct.ci = *ci;
			globalCalibrationStruct.calibrationTrigger = true;
			globalCalibrationStruct.from_saved_file = true;
			b->calibrationLock.unlock();
			sendCodeResponse(i, SUCCESS, ""); // send success back
			HeapFree(heap, HEAP_FREE_CHECKING_ENABLED, ci);
			ci = NULL;
		}
		break;
	}
	case IS_CALIBRATED: {
		printf("Received is calibrated request\n");
		if (globalCalibrationStruct.gloveCalibrated && gloveFound) {
			sendCodeResponse(i, SUCCESS, "yes");
		}
		else {
			sendCodeResponse(i, SUCCESS, "no");
		}
		break;
	}
	case IS_GLOVE_CONNECTED: {
		// respond with gloveFound with code response
		if (gloveFound)
			sendCodeResponse(i, SUCCESS, "yes");
		else
			sendCodeResponse(i, SUCCESS, "no");
		break;
	}
	case START_RECORDING: {
		// This is a temporary endpoint that will be removed once we develop the gestural recognition
		// This will tell the bluetooth manager to start "recording" our normalized data - given that we are connected and calibrated
		if (globalCalibrationStruct.gloveCalibrated && gloveFound) {
			if (b->startRecording()) {
				sendCodeResponse(i, SUCCESS, "");
			}
			else {
				sendCodeResponse(i, FAILURE, "Could not start recording");
			}
		}
		else {
			sendCodeResponse(i, FAILURE, "Need to be connected and calibrated");
		}
	}
	case END_RECORDING: {
		// We expect a payload with a filepath here
		if (globalCalibrationStruct.gloveCalibrated && gloveFound) {
			// read filepath from request
			char filepath[BUFFER_SIZE];
			strncpy_s(filepath, BUFFER_SIZE, requestBytes + 1, BUFFER_SIZE);
			for (int i = 0; i < strlen(filepath); i++) {
				if (filepath[i] == '\n') {
					filepath[i] = '\0'; // need to remove the newline so that is not in the filepath
				}
			}

			if (b->endRecording(filepath)) {
				sendCodeResponse(i, SUCCESS, "");
			}
			else {
				sendCodeResponse(i, FAILURE, "Failed to end recording");
			}
		}
		else {
			sendCodeResponse(i, FAILURE, "Need to be connected and calibrated");
		}
	}
	default: break;
	}
	return SUCCESS_RET;
}

/*
 * newNamedPipe is used to create a communication channel for a new client to listen to gestures
 * Inputs: string processName (the unique id for the client), ProcListener *pl (the list of all named pipes)
 * Returns: bool (was it made succesfully or not)
 */
bool newNamedPipe(string processName, ProcListener *pl) {
	for (vector<ProcListener>::iterator it = listeners.begin(); it < listeners.end(); it++) {
		if ((*it).procName == processName) {
			return false;
		}
	}

	// no match found, make new handle
	LPCWSTR pipeName;
	wstring procName = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(processName);
	wstring wName = PIPE_DIR + procName;
	pipeName = wName.c_str();
	printf("Pipe name is %ws \n", pipeName);

	// Create new SECURITY_ATTRIBUTES and SECURITY_DESCRIPTOR structure objects
	SECURITY_ATTRIBUTES sa;
	SECURITY_DESCRIPTOR sd;

	// Initialize the new SECURITY_DESCRIPTOR object to empty values
	if (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) == 0)
	{
		printf("InitializeSecurityDescriptor failed with error %d\n", GetLastError());
		return false;
	}

	// Set the DACL field in the SECURITY_DESCRIPTOR object to NULL
	if (SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE) == 0)
	{
		printf("SetSecurityDescriptorDacl failed with error %d\n", GetLastError());
		return false;
	}

	// Assign the new SECURITY_DESCRIPTOR object to the SECURITY_ATTRIBUTES object
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = TRUE;
	HANDLE namedPipe = CreateNamedPipe(pipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE, 1, 0, 0, 1000, &sa);
	if (namedPipe == INVALID_HANDLE_VALUE) {
		printf("createnamedpipe failed\n");
		return false;
	}

	pl->namedPipe = namedPipe;
	pl->namedPipePath = (const char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, S_PIPE_DIR.length() + processName.length() + 1);
	assert(pl->namedPipePath != NULL);
	string namedPipePath = S_PIPE_DIR + processName;
	memcpy_s((void *)pl->namedPipePath, namedPipePath.length(), namedPipePath.c_str(), namedPipePath.length());
	pl->procName = (const char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, processName.length() + 1);
	assert(pl->procName != NULL);
	memcpy_s((void *)pl->procName, processName.length(), processName.c_str(), processName.length());
	printf("named pipe is %s", pl->namedPipePath);
	return true;
}

/*
 * sendCodeResponse is used to send a success or error response over the socket to a client
 * Input: SOCKET i (socket to send on), char code (success or error code), const char *response (response to send with the code)
 * Returns: int (ERROR or SUCCESS)
 */
int sendCodeResponse(SOCKET i, char code, const char *response) {
	// construct payload
	char buf[BUFFER_SIZE];
	memset(buf, '\0', BUFFER_SIZE);
	buf[0] = code;
	int responseLength = strlen(response);
	if (responseLength > BUFFER_SIZE - 2) {
		// bad response
		return ERROR_RET;
	}
	strncpy_s(buf + 1, BUFFER_SIZE - 2, response, (size_t)responseLength);
	buf[responseLength + 1] = '\n';
	buf[responseLength + 2] = '\0';

	// send payload over socket
	printf("Response is %s\n", response);
	printf("Payload to send is %s \n", buf);
	if (send(i, buf, responseLength + 2, 0) == SOCKET_ERROR) {
		return ERROR_RET;
	}
	return SUCCESS_RET;
}

/*
 * freeProcListener is used to release a namedPipe when a client disconnects
 * Input: SOCKET i (client that disconnected)
 * Returns: void
 */
void freeProcListener(SOCKET i) {
	for (vector<ProcListener>::iterator it = listeners.begin(); it < listeners.end(); it++) {
		if ((*it).socket == i) {
			// correct process found - remove from listeners, close namedpipe, then return ERROR_RET so main handler closes socket
			DisconnectNamedPipe((*it).namedPipe);
			it = listeners.erase(it);
			break;
		}
	}
}

/*
 * errorCallback used to pass in to the bluetooth manager - when there is an error in connecting,
 * we just try to reconnect
 */
void bluetoothErrorHandler() {
	// We set the global variable RETRY_FIND_GLOVE to true to tell our select loop to retry since we don't
	// have access to the bluetooth manager in this function
	gloveFound = false;
	zeroCalibrationStruct();
	RETRY_FIND_GLOVE = true;
}

void zeroCalibrationStruct() {
	globalCalibrationStruct.from_saved_file = false;
	globalCalibrationStruct.gloveCalibrated = false;
	globalCalibrationStruct.ci = { {0, 0, 0, 0, 0}, {0, 0, 0 ,0 ,0} };
	globalCalibrationStruct.calibrationTrigger = false;
	globalCalibrationStruct.calibrationStarted = false;
	memset(&(globalCalibrationStruct.savedFilePath[0]), '\0', MAX_FILEPATH_LENGTH);
}

// Used to read calibration data from a saved file
// Returns false if bad format file
bool getCalibrationInfoFromFile(char* filepath, CalibrationInfo* ci) {
	ifstream infile(filepath);

	if (infile.bad()) {
		return false;
	}

	char buf[BUFFER_SIZE];
	memset(buf, '\0', BUFFER_SIZE);
	int count = 0;
	try {
		// if correctly formatted, first line is space seperated min values, second line is space separated max values
		/* File should look like:
		1 2 3 4 5
		1 2 3 4 5
		*/
		infile.getline(buf, BUFFER_SIZE);
		char* next_token;
		char* token = strtok_s(buf, " \n", &next_token);
		while (token != NULL && count < 5) {
			int threadVal = atoi(token);
			if (threadVal < 0) {
				// not a number
				return false;
			}
			ci->minReading[count] = (uint16_t)threadVal;
			count++;
			token = strtok_s(buf, " \n", &next_token);
		}

		// First line was not formatted correctly
		if (count != 5) {
			return false;
		}

		// all of min readings have been read - reset buffer, count and read the next line
		memset(buf, '\0', BUFFER_SIZE);
		count = 0;
		infile.getline(buf, BUFFER_SIZE);
		token = strtok_s(buf, " \n", &next_token);
		while (token != NULL && count < 5) {
			int threadVal = atoi(token);
			if (threadVal < 0) {
				// not a number
				return false;
			}
			ci->maxReading[count] = (uint16_t)threadVal;
			count++;
			token = strtok_s(buf, " \n", &next_token);
		}

		// First line was not formatted correctly
		if (count != 5) {
			return false;
		}
	}
	catch (std::ifstream::failure e) {
		return false;
	}
}

// Returns the string format to save in a file for a given calibrationinfo struct
char* createCalibrationPayload(CalibrationInfo ci) {
	char* payload = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, BUFFER_SIZE);
	// Min readings first, then max
	if (sprintf_s(payload, BUFFER_SIZE, "%u %u %u %u %u\n%u %u %u %u %u\n\0",
		ci.minReading[0], ci.minReading[1], ci.minReading[2], ci.minReading[3], ci.minReading[4],
		ci.maxReading[0], ci.maxReading[1], ci.maxReading[2], ci.maxReading[3], ci.maxReading[4]) < 0) 
	{
		HeapFree(heap, HEAP_FREE_CHECKING_ENABLED, payload);
		return NULL;
	}
	return payload;
}