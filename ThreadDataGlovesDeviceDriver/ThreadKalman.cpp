#include <cstdlib>
#include <cstdio>
#include <stdint.h>
#include <math.h>
#include "ThreadKalman.hpp"

using namespace std;

ThreadKalman::ThreadKalman(float _A, float _Q, float _R, float _P, uint16_t _H, uint16_t _xhat) {
	A = _A;
	Q = _Q;
	R = _R;
	P = _P;
	H = _H;
	currx = _xhat;
}

ThreadKalman::~ThreadKalman() {
	return;
}

void ThreadKalman::update(uint16_t newMeas) {
	/*Prediction Step*/
	int16_t xhat = (int16_t) (A * currx);
	float Pprime = A * P * A + Q;

	/*Observation Step*/
	int16_t innov = newMeas - (H * xhat);
	float innov_cov = H * Pprime * H + R;

	/*Update Step*/
	float kalmGain = Pprime * H * (1 / innov_cov);
	int16_t newX = xhat + (kalmGain * innov);
	A = ((float)newX) / currx;
	currx = newX;
	P = (1 - kalmGain * H) * Pprime;
	fprintf(stderr, "xhat %u, pp %f, i %d, ic %f, kg %f, nx %u, A %f, P %f\n", xhat, Pprime, innov, innov_cov, kalmGain, newX, A, P);

}

uint16_t ThreadKalman::getValue() {
	return currx;
}

float ThreadKalman::getError() {
	return P;
}