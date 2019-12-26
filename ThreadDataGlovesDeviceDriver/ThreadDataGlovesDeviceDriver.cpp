// ThreadDataGlovesDeviceDriver.cpp : This file contains the 'main' function. Program execution begins and ends there.

#ifndef WIN32
	#define WIN32
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include "BluetoothManager.h"
#include "GestureRecognizer.h"
#include <iostream>
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

#define PORT 10500
#define BUFFER_SIZE 1000
#define FD_SETSIZE 1024

using namespace std; 

const int ERROR_RET = -1;
const int SUCCESS_RET = 1;
const int ASCII_NUM_VAL = 48;
const wstring PIPE_DIR = L"\\\\.\\PIPE\\";
const string S_PIPE_DIR = "\\\\.\\PIPE\\";

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

enum RequestCodes {
	HI=1, BYE, BATTERY_LIFE, START_CALIBRATION, END_CALIBRATION, USE_SAVED_CALIBRATION_DATA, IS_CALIBRATED
};

enum ReturnCodes {
	FAILURE=1, SUCCESS=2
};

/******  Function Declarations  *******/
void gestureListener(SensorInfo s);
int processRequest(SOCKET i, char *requestBytes, BluetoothManager *b);
bool newNamedPipe(string processName,  ProcListener *pl);
int sendCodeResponse(SOCKET i, char code, const char *response);
void calibrate(BluetoothManager* b, CalibrationInfo &result);
void freeProcListener(SOCKET i);

// global variable which is linked list of structs that contain process name & named pipes used for gesture listening
vector<ProcListener> listeners;

// global heap
HANDLE heap;

// global gesture recognizer
GestureRecognizer* gestureRecognizer;

// global glove found
bool gloveFound = false;

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

	CoInitializeSecurity(
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
	BluetoothManager bluetoothMngr(false);

	// initialize gesture recognizer with bluetooth manager instance passed by ref.
	gestureRecognizer = new GestureRecognizer(&bluetoothMngr);

	// start find glove, pass in listener. When listener called, we are connected with the glove.
	bluetoothMngr.findGlove((listenCallback)gestureListener);
	
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
		}
	}

	return 0;
}

void gestureListener(SensorInfo s) {
	// do some gesture recognition based off of accumulated time series
	gloveFound = true;
	Gesture g = gestureRecognizer->recognize();
	g.x = s.gyroscope[0];
	g.y = s.gyroscope[1];
	g.z = s.gyroscope[2];

	printf("Gesture received! \n");

	// make payload
	TCHAR buffer[128] = { L'0' };
	buffer[0] = (int)g.gestureCode + ASCII_NUM_VAL;
	buffer[1] = ' ';
	buffer[2] = g.x + ASCII_NUM_VAL;
	buffer[3] = ',';
	buffer[4] = g.y + ASCII_NUM_VAL;
	buffer[5] = ',';
	buffer[6] = g.z + ASCII_NUM_VAL;
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

int processRequest(SOCKET i, char *requestBytes, BluetoothManager* b) {
	printf("in process request \n");
	printf("code is %d \n", requestBytes[0]);
	switch (requestBytes[0]) {
	//TODO: handle requests
	//TODO: better error handling
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
		// no-op for now
		break;
	}
	case END_CALIBRATION: {
		// no-op for now
		break;
	}
	case USE_SAVED_CALIBRATION_DATA: {
		// no-op for now
		break;
	}
	case IS_CALIBRATED: {
		// no-op for now
		break;
	}
	}
	return SUCCESS_RET;
}

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

void calibrate(BluetoothManager* b, CalibrationInfo& result) {
	// Called in thread
	// TODO: run b->listen() until some notification from main, and then store max and min in result
}

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