// ThreadDataGlovesDeviceDriver.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "BluetoothManager.h"
#include "GestureRecognizer.h"
#include <future>
#include <thread>
#include <vector>
#include <Windows.h>
#include <string>
#include <atlstr.h>
#include <WinSock2.h>

#define PORT 10500
#define BUFFER_SIZE 1000

using namespace std;

struct ProcListeners {
	string namedPipePath;
	HANDLE namedPipe;
	string procName;
} typedef ProcListeners;

enum RequestCodes {
	HI=0, BYE, BATTERY_LIFE, START_CALIBRATION, END_CALIBRATION, USE_SAVED_CALIBRATION_DATA, IS_CALIBRATED
};

/******  Function Declarations  *******/
void gestureListener(GestureRecognizer *gestureRecognizer);
int processRequest(SOCKET i, string request, BluetoothManager *b);
void calibrate(BluetoothManager* b, CalibrationInfo &result);

// global variable which is linked list of structs that contain process name & named pipes used for gesture listening
vector<ProcListeners> listeners;

int main()
{
	INT Ret;
	WSADATA wsaData;
	SOCKET ListenSocket;
	SOCKET AcceptSocket;
	SOCKADDR_IN InternetAddr;
	FD_SET ActiveFdSet;
	FD_SET ReadFdSet;
	ULONG NonBlock;

	// check to make sure we can do network communication
	if ((Ret = WSAStartup(0x0202, &wsaData)) != 0)
	{
		printf("WSAStartup() failed with error %d\n", Ret);
		WSACleanup();
		return 1;
	}

	// initialize bluetooth manager
	BluetoothManager bluetoothMngr;

	// initialize gesture recognizer with bluetooth manager instance passed by ref.
	GestureRecognizer* gestureRecognizer = new GestureRecognizer(&bluetoothMngr);

	// asyncrhonously ask bluetooth manager to find a glove
	// on return, assign value to variable that indicates we have a glove connection, set calibration to false
	// these values need to be passed to gesture recognizer
	future<Glove> gloveResult = async(launch::async, bluetoothMngr.findGlove);

	// start thread for gesture listening 
	thread gestureListenerThread(gestureListener, gestureRecognizer);

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

	// Change the socket mode on the listening socket from blocking to
	// non-block so the application will not block waiting for requests
	NonBlock = 1;
	if (ioctlsocket(ListenSocket, FIONBIO, &NonBlock) == SOCKET_ERROR)
	{
		printf("ioctlsocket() failed with error %d\n", WSAGetLastError());
		return 1;
	}
	
	FD_ZERO(&ActiveFdSet);
	FD_SET(ListenSocket, &ActiveFdSet);

	// timeout
	struct timeval tv;
	tv.tv_sec = 60;
	tv.tv_usec = 0;

	// initialize read buffer array
	string buffers[FD_SETSIZE] = { "" };
	
	int ret;
	char buffer[BUFFER_SIZE + 1];
	while (1) {
		ReadFdSet = ActiveFdSet;
		ret = select(FD_SETSIZE, &ReadFdSet, NULL, NULL, &tv);
		if (ret < 0) {
			// select failed
			perror("select");
		}
		else if (ret == 0) {
			// timeout occured - check if glove found
			future_status status = gloveResult.wait_for(chrono::microseconds(1));
			if (status == future_status::ready) {
				Glove g = gloveResult.get();
				gestureRecognizer->setGlove(g);
			}
		}
		else {
			// service all input pending sockets
			for (int i = 0; i < FD_SETSIZE; i++) {
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
					}
					else {
						// handle endpoint request or closed socket
						memset(buffer, 0, BUFFER_SIZE);
						int recvRet = recv(i, buffer, BUFFER_SIZE, 0);
						if (recvRet <= 0) {
							// close connection
							closesocket(i);
							FD_CLR(i, &ActiveFdSet);
						}
						else {
							// if we have the end of a request, process it, otherwise store the request
							if (find(buffer, buffer + recvRet, '\n') != buffer + recvRet) {
								// end of a request
								string buf(buffer);
								string request = buffers[i] + buf;
								if (processRequest(i, request, &bluetoothMngr) < 0) {
									closesocket(i);
									FD_CLR(i, &ActiveFdSet);
								}
								buffers[i] = "";
							}
							else {
								string buf(buffer);
								buffers[i] += buf;
							}
						}
					}
				}
			}
		}
	}

	// this purposely never gets reached
	gestureListenerThread.join();
}

void gestureListener(GestureRecognizer *gestureRecognizer) {
	while (true) {
		Gesture g = gestureRecognizer->recognize();

		// make payload
		string buf = to_string(g.gestureCode) + " " + to_string(g.x) + "," + to_string(g.y) + "," + to_string(g.z) + "\n";
		TCHAR buffer[64];
		size_t bufSize = buf.size();
		_tcsncpy_s(buffer, CA2T(buf.c_str()), bufSize);
		

		// received gesture, now send over named pipes
		for (unsigned i = 0; i < listeners.size(); i++) {
			ProcListeners l = listeners.at(i);
			if (l.namedPipe != INVALID_HANDLE_VALUE) {
				DWORD bytesWritten;
				WriteFile(l.namedPipe, buffer, bufSize, &bytesWritten, NULL);
			}
		}
	}
} 

int processRequest(SOCKET i, string request, BluetoothManager* b) {
	const char* requestBytes = request.c_str();
	switch (requestBytes[0]) {
	//TODO: handle requests
	case HI: {

		break;
	}
	case BYE: {

		break;
	}
	case BATTERY_LIFE: {

		break;
	}
	case START_CALIBRATION: {

		break;
	}
	case END_CALIBRATION: {

		break;
	}
	case USE_SAVED_CALIBRATION_DATA: {

		break;
	}
	case IS_CALIBRATED: {
		break;
	}
	}
}

void calibrate(BluetoothManager* b, CalibrationInfo& result) {
	// Called in thread
	// TODO: run b->listen() until some notification from main, and then store max and min in result
}