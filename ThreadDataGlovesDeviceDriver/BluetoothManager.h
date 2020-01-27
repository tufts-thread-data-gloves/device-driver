#include "GestureRecognizer.h"

typedef void (*listenCallback)(Gesture *arg);
typedef void (*errorCallback)();

class BluetoothManager
{
public:
	BluetoothManager(HANDLE *heapPtr);
	~BluetoothManager();
	void findGlove(listenCallback c, errorCallback e, bool *globalGloveFound);
	void setConnected(bool c);

private:
	bool connected;
	GestureRecognizer *recognizer;
};

