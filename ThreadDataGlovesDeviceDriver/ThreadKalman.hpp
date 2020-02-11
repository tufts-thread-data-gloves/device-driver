#include <cstdlib>
#include <cstdio>
#include <stdint.h>

#ifndef THREADKALMAN_H
#define THREADKALMAN_H

using namespace std;

class ThreadKalman {
public :
	ThreadKalman(float _A, float _Q, float _R, float _P, uint16_t _H, uint16_t _xhat);
	~ThreadKalman();
	void update(uint16_t newMeas);
	uint16_t getValue();
	float getError();
private:
	float A, Q, R, P;
	uint16_t H, currx;
};

#undef THREADKALMAN_H
#endif