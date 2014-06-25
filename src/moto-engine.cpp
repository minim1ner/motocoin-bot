// Copyright (c) 2014 The Motocoin developers
// pributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//--------------------------------------------------------------------
// This file is part of The Game of Motocoin, Motocoin-Qt and motocoind.
//--------------------------------------------------------------------
// Physics engine for motogame.
// It is full of integer magic for determinism.
// It would be hard to get deterministic behaviour with floating point.
//--------------------------------------------------------------------

#include <memory.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <debug.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
#endif

using namespace std;

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <exception>
#include <sstream>


#ifdef NO_OPENSSL_SHA
    extern void *SHA512(uint8_t *buffer, size_t len, void *resblock);
#else
    #include <openssl/sha.h>
#endif

#include "moto-engine.h"
#include "moto-engine-const.h"

/* Lookup tables. */
static bool g_TablesInitialized = false;
#define g_SqrtTableSize 150000
static uint16_t g_s[0x10000];
static uint16_t g_ds_div_2[0x10000];
static int32_t g_inv_sqrt[g_SqrtTableSize];

static inline int32_t sin16_32(int16_t a)
{
	if (a <= -16384)
		return -g_sin[32768 + a];
	else if (a < 0)
		return -g_sin[-a];
	else if (a < 16384)
		return g_sin[a];
	else
		return g_sin[32768 - a];
}

static inline int32_t cos16_32(int16_t a)
{
	if (a <= -16384)
		return -g_sin[-16384 - a];
	else if (a < 0)
		return g_sin[16384 + a];
	else if (a < 16384)
		return g_sin[16384 - a];
	else
		return -g_sin[a - 16384];
}

static inline int sign(int32_t val)
{
	return (0 < val) - (val < 0);
}

static inline int32_t min32(int32_t a, int32_t b)
{
	return (a < b) ? a : b;
}

static inline uint16_t muluu(uint16_t a, uint16_t b)
{
	return ((uint32_t)a*(uint32_t)b) >> 16;
}

static inline int16_t mulsu(int16_t a, uint16_t b)
{
	return ((int32_t)a*(int32_t)b) >> 16;
}

static inline int32_t mulsu32(int32_t a, uint32_t b)
{
	return ((int64_t)a*(int64_t)b) >> 31;
}

static inline int32_t mulss32(int32_t a, int32_t b)
{
	return ((int64_t)a*(int64_t)b) >> 31;
}

/* 
Integer aquare root. This is used only for lookup table initialization.
We don't use floating point sqrt to ensure that there is no rounding differences in implementations,
however, this probably isn't required.
https://en.wikipedia.org/wiki/Methods_of_computing_square_roots
*/
static uint64_t isqrt(uint64_t num)
{
	uint64_t res = 0;
	uint64_t bit = ((uint64_t)1) << 62; /* The second-to-top bit is set */

	/* "bit" starts at the highest power of four <= the argument. */
	while (bit > num)
		bit >>= 2;

	while (bit != 0)
	{
		if (num >= res + bit)
		{
			num -= res + bit;
			res = (res >> 1) + bit;
		}
		else
			res >>= 1;
		bit >>= 2;
	}
	return res;
}

static void initTables()
{
	if (g_TablesInitialized)
		return;
	g_TablesInitialized = true;

	for (int64_t i = 0; i < 0x10000; i++)
	{
		g_s[i] = (uint16_t)((i*i*(3*65536 - 2*i)) >> 32);
		g_ds_div_2[i] = (uint16_t)((3*i*(65536 - i)) >> 16);
	}

	for (int i = 0; i < g_SqrtTableSize; i++)
		g_inv_sqrt[i] = (int32_t)(17592186036224/isqrt((i + 1)*68719476736));
}

static int16_t at8192_4096(const MotoWorld* pWorld, int16_t grad[2], const int32_t P[2])
{
	uint64_t x64 = (uint32_t)(P[0])*(int64_t)(MOTO_MAP_SIZE);
	uint64_t y64 = (uint32_t)(P[1])*(int64_t)(MOTO_MAP_SIZE);
	int i0 = x64 >> 32;
	int i1 = (i0 + 1) % MOTO_MAP_SIZE;
	int j0 = y64 >> 32;
	int j1 = (j0 + 1) % MOTO_MAP_SIZE;

	int32_t x = (x64 % 4294967296) >> 10;
	int32_t y = (y64 % 4294967296) >> 10;
	uint16_t sx = g_s[x >> 6];
	uint16_t sy = g_s[y >> 6];
	uint16_t sxsy = muluu(sx, sy);
	uint16_t dsx_div_2 = g_ds_div_2[x >> 6];
	uint16_t dsy_div_2 = g_ds_div_2[y >> 6];
	int8_t x00 = pWorld->Map[i0][j0][0];
	int8_t y00 = pWorld->Map[i0][j0][1];
	int8_t x01 = pWorld->Map[i0][j1][0];
	int8_t y01 = pWorld->Map[i0][j1][1];
	int8_t x10 = pWorld->Map[i1][j0][0];
	int8_t y10 = pWorld->Map[i1][j0][1];
	int8_t x11 = pWorld->Map[i1][j1][0];
	int8_t y11 = pWorld->Map[i1][j1][1];
	int16_t Q00 = (x00*x + y00*y) >> 16;
	int16_t Q01 = (x01*x + y01*(y - 4194304)) >> 16;
	int16_t Q11 = (x11*(x - 4194304) + y11*(y - 4194304)) >> 16;
	int16_t Q10 = (x10*(x - 4194304) + y10*y) >> 16;
	int16_t Q1 = Q10 - Q00;
	int16_t Q2 = Q01 - Q00;
	int16_t Q3 = Q00 - Q01 - Q10 + Q11;
	int16_t Q4 = Q2 + mulsu(Q3, sx);
	int16_t Q5 = Q1 + mulsu(Q3, sy);
	int16_t f = (Q00  + mulsu(Q1, sx) + mulsu(Q4, sy));
	grad[0] = (((x00 + mulsu(x10 - x00, sx) + mulsu(x01 - x00, sy) + mulsu(x00 - x01 - x10 + x11, sxsy)) << 5) + mulsu(Q5, dsx_div_2));
	grad[1] = (((y00 + mulsu(y10 - y00, sx) + mulsu(y01 - y00, sy) + mulsu(y00 - y01 - y10 + y11, sxsy)) << 5) + mulsu(Q4, dsy_div_2));

	// ensure it is not zero
	grad[0] |= 1;
	grad[1] |= 1;

	return f;
}

static int16_t getF(const MotoWorld* pWorld, const int32_t P[2])
{
    uint64_t x64 = (uint32_t)(P[0])*(int64_t)(MOTO_MAP_SIZE);
    uint64_t y64 = (uint32_t)(P[1])*(int64_t)(MOTO_MAP_SIZE);
    int i0 = x64 >> 32;
    int i1 = (i0 + 1) % MOTO_MAP_SIZE;
    int j0 = y64 >> 32;
    int j1 = (j0 + 1) % MOTO_MAP_SIZE;

    int32_t x = (x64 % 4294967296) >> 10;
    int32_t y = (y64 % 4294967296) >> 10;
    uint16_t sx = g_s[x >> 6];
    uint16_t sy = g_s[y >> 6];
    int8_t x00 = pWorld->Map[i0][j0][0];
    int8_t y00 = pWorld->Map[i0][j0][1];
    int8_t x01 = pWorld->Map[i0][j1][0];
    int8_t y01 = pWorld->Map[i0][j1][1];
    int8_t x10 = pWorld->Map[i1][j0][0];
    int8_t y10 = pWorld->Map[i1][j0][1];
    int8_t x11 = pWorld->Map[i1][j1][0];
    int8_t y11 = pWorld->Map[i1][j1][1];
    int16_t Q00 = (x00*x + y00*y) >> 16;
    int16_t Q01 = (x01*x + y01*(y - 4194304)) >> 16;
    int16_t Q11 = (x11*(x - 4194304) + y11*(y - 4194304)) >> 16;
    int16_t Q10 = (x10*(x - 4194304) + y10*y) >> 16;
    int16_t Q1 = Q10 - Q00;
    int16_t Q2 = Q01 - Q00;
    int16_t Q3 = Q00 - Q01 - Q10 + Q11;
    int16_t Q4 = Q2 + mulsu(Q3, sx);
    int16_t f = (Q00  + mulsu(Q1, sx) + mulsu(Q4, sy));


    return f;
}

void motoF(float Fdxdy[3], float x, float y, const MotoWorld* pWorld)
{
	// map from [0, 1] to [-0.5, 0.5].
	if (x > 0.5f)
		x -= 1.0f;
	if (y > 0.5f)
		y -= 1.0f;
	const int32_t P[2] = { (int32_t)(x*4294967296.0f + 0.5), (int32_t)(y*4294967296.0f + 0.5) };
	int16_t grad[2];
	int16_t F = at8192_4096(pWorld, grad, P);
	Fdxdy[0] = grad[0]/4096.0f;
	Fdxdy[1] = grad[1]/4096.0f;
	Fdxdy[2] = F/8192.0f;
}

static int32_t getGroundCollideDist65536(const int32_t Pos[2], const MotoWorld* pWorld)
{
	int16_t grad2[2];
	int16_t f = at8192_4096(pWorld, grad2, Pos);
	if (f > MOTO_LEVEL)
		return 0;

	int32_t invgradlen = g_inv_sqrt[min32((grad2[0]*grad2[0] + grad2[1]*grad2[1]) >> 8, g_SqrtTableSize - 1)];
	int32_t t65536 = ((int64_t)(MOTO_LEVEL - f)*MOTO_SCALE*(int64_t)(invgradlen)) >> 15;
	return t65536;
}

static bool advanceWheel(MotoBody* pWheel, int64_t WheelF[2], int64_t WheelM, const MotoWorld* pWorld)
{
	int16_t grad2[2];
	int16_t f = at8192_4096(pWorld, grad2, pWheel->Pos);
	if (f > MOTO_LEVEL)
		return false;

	int32_t invgradlen = g_inv_sqrt[min32((grad2[0]*grad2[0] + grad2[1]*grad2[1]) >> 8, g_SqrtTableSize - 1)]; // (int32_t)(2147483647.0/sqrt((double)(grad2[0]*grad2[0] + grad2[1]*grad2[1])));
	int32_t t65536 = ((int64_t)(MOTO_LEVEL - f)*MOTO_SCALE*(int64_t)(invgradlen)) >> 15;
	if (t65536 < g_65536WheelR)
	{
		int32_t grad[2] = { grad2[0]*invgradlen, grad2[1]*invgradlen };
		int32_t k = g_MotoWheelR_div_PosK - t65536*g_1_div_65536PosK;
		pWheel->Pos[0] -= mulss32(grad[0], k)*2;
		pWheel->Pos[1] -= mulss32(grad[1], k)*2;
		int32_t Speed = -mulss32(grad[0], pWheel->Vel[0])*2 - mulss32(grad[1], pWheel->Vel[1])*2;
		if (Speed < -g_001dt_div_PosK)
		{
			pWheel->Vel[0] += mulss32(Speed, grad[0])*2;
			pWheel->Vel[1] += mulss32(Speed, grad[1])*2;
			int32_t N = mulss32(grad[0], (int32_t)(WheelF[0]/g_fhh))*2 + mulss32(grad[1], (int32_t)(WheelF[1]/g_fhh))*2;
			if (N > 0)
			{
				int32_t CurVel = mulss32(pWheel->Vel[0], grad[1])*2 - mulss32(pWheel->Vel[1], grad[0])*2 - pWheel->AngVel/g_khgjk;
				int32_t k = sign(CurVel)*min32(10*N, abs(CurVel));
				WheelM += (int64_t)(k)*g_MotoWheelR_PosK_div_AngPosK_fhh;
				WheelF[0] -= mulss32(grad[1], k)*(int64_t)(g_fhh)*2;
				WheelF[1] += mulss32(grad[0], k)*(int64_t)(g_fhh)*2;
			}
		}
	}

	/* Integrate wheel position and velocity. */
	pWheel->AngPos += pWheel->AngVel;
	pWheel->AngVel += (int32_t)(WheelM/g_WheelAngularMass_div_dt);
	pWheel->Pos[0] += pWheel->Vel[0];
	pWheel->Pos[1] += pWheel->Vel[1];
	pWheel->Vel[0] += (int32_t)(WheelF[0]/g_WheelMass_div_dt);
	pWheel->Vel[1] += (int32_t)(WheelF[1]/g_WheelMass_div_dt);
	return true;
}

static void computeBikeWheelForces(const MotoBody* pBike, const MotoBody* pWheel, const int32_t BN[2], int64_t WheelF[2], int64_t BikeF[2], int64_t* pBikeM)
{
	int32_t BW[2] = { pWheel->Pos[0] - pBike->Pos[0], pWheel->Pos[1] - pBike->Pos[1] };
	int32_t WN[2] = { BN[0] - BW[0], BN[1] - BW[1] };

	//	static const int64_t g_1_div_PosK2 = int64_t(1/(g_MotoPosK*g_MotoPosK));
	//	int64_t Dist2 = int64_t(BW[0])*int64_t(BW[0]) + int64_t(BW[1])*int64_t(BW[1]);

	int32_t v[2] = { -(int32_t)((int64_t)(BW[1])*(int64_t)(pBike->AngVel)/g_InvAngPosK) + pBike->Vel[0] - pWheel->Vel[0], (int32_t)((int64_t)(BW[0])*(int64_t)(pBike->AngVel)/g_InvAngPosK) + pBike->Vel[1] - pWheel->Vel[1] };
	WheelF[0] += (int64_t)(WN[0])*g_WheelK + (int64_t)(v[0])*g_WheelK0;// +int64_t(BW[1]) * WheelM/(int64_t(g_InvAngPosK)*Dist2)*g_1_div_PosK2;
	WheelF[1] += (int64_t)(WN[1])*g_WheelK + (int64_t)(v[1])*g_WheelK0;// -int64_t(BW[0]) * WheelM/(int64_t(g_InvAngPosK)*Dist2)*g_1_div_PosK2;
	*pBikeM -= ((int64_t)(BN[0])*(int64_t)(WN[1]) - (int64_t)(BN[1])*(int64_t)(WN[0]))/g_InvK + (-(int64_t)(v[0])*(int64_t)(BW[1]) + (int64_t)(v[1])*(int64_t)(BW[0]))/g_InvK0;

	BikeF[0] -= WheelF[0];
	BikeF[1] -= WheelF[1];
}

static void computeBikeHeadForces(const MotoState* pState, const int32_t BN[2], int64_t HeadF[2])
{
	int32_t BH[2] = { pState->HeadPos[0] - pState->Bike.Pos[0], pState->HeadPos[1] - pState->Bike.Pos[1] };
	int32_t HN[2] = { BN[0] - BH[0], BN[1] - BH[1] };

	int16_t K = g_WheelK/5;
	if ((int64_t)(HN[0])*(int64_t)(HN[0]) + (int64_t)(HN[1])*(int64_t)(HN[1]) >= g_HeadPosThreshold)
		K = 2*g_WheelK;

	int32_t v[2] = { -(int32_t)((int64_t)(BH[1])*(int64_t)(pState->Bike.AngVel)/g_InvAngPosK) + pState->Bike.Vel[0] - pState->HeadVel[0], (int32_t)((int64_t)(BH[0])*(int64_t)(pState->Bike.AngVel)/g_InvAngPosK) + pState->Bike.Vel[1] - pState->HeadVel[1] };
	HeadF[0] += (int64_t)(HN[0])*K + (int64_t)(v[0])*g_WheelK0/5;
	HeadF[1] += (int64_t)(HN[1])*K + (int64_t)(v[1])*g_WheelK0/5;
}

int32_t distSq(const int32_t A[2], const int32_t B[2]){
    return (((int64_t)(A[0] - B[0])*(int64_t)(A[0] - B[0])) >> 32) + (((int64_t)(A[1] - B[1])*(int64_t)(A[1] - B[1])) >> 32);
}

static bool isDistLess(const int32_t A[2], const int32_t B[2], int32_t Dist)
{
    return distSq(A,B) < (((int64_t)(Dist)*(int64_t)(Dist)) >> 32);
}

int64_t distSqp(const int32_t A[2], const int32_t B[2]){
    int64_t D=((int64_t)1)<<32;
    int64_t playerY = A[1];
    int64_t finishY=B[1];
    int64_t finishX=B[0];
    int64_t playerX=A[0];

    if(playerY<0){
      playerY+=D;
    }

        int64_t dx=(finishX-playerX);
        int64_t dy=(playerY - finishY);
        int64_t dist1=((dx/8*dx) >> 32) + ((dy/8*dy) >> 32);
        int64_t dist2=(((dx-D)/8*(dx-D)) >> 32) + ((dy/8*dy) >> 32);
          return min(dist1,dist2);
//        return dist1;
}

static EMotoResult advanceOneFrame(MotoState* pState, EMotoAccel Accel, EMotoRot Rotation, const MotoWorld* pWorld)
{
	initTables();

	if (pState->Dead)
		return MOTO_FAILURE;

	pState->iFrame++;
	if (pState->iFrame == MOTO_MAX_FRAMES)
	{
		pState->Dead = true;
		return MOTO_FAILURE;
	}

	if (Rotation != MOTO_NO_ROTATION)
	{
	    /* Check input for consistency. */
		if (pState->iFrame - pState->iLastRotate < g_RotationPeriod)
			return MOTO_FAILURE;
		pState->iLastRotate = pState->iFrame; /* Remember when rotation was started. */
		pState->Rotation = Rotation;
	}

	/* Update state. */
	pState->Accel = Accel;

	/* Forces and torques. */
	int64_t WheelF[2][2];
	int64_t BikeF[2];
	int64_t WheelM[2] = { 0, 0 };
	int64_t BikeM = 0;
	int64_t HeadF[2];
	WheelF[0][0] = 0;
	WheelF[0][1] = g_g_mul_WheelMass_mul_dt_div_PosK;
	WheelF[1][0] = 0;
	WheelF[1][1] = g_g_mul_WheelMass_mul_dt_div_PosK;
	BikeF[0] = 0;
	BikeF[1] = g_g_mul_BikeMass_mul_dt_div_PosK;
	HeadF[0] = 0;
	HeadF[1] = g_g_mul_HeadMass_mul_dt_div_PosK;

	/* Initiate rotation. */
	if (pState->iLastRotate == pState->iFrame)
	{
		if (pState->Rotation == MOTO_ROTATE_CW)
			BikeM -= g_RotationSpeedFast_mul_g_BikeAngularMass_div_g_MotoAngPosK;
		else
			BikeM += g_RotationSpeedFast_mul_g_BikeAngularMass_div_g_MotoAngPosK;
	}
	/* Slow down rotation after 1/4 of its period. */
	else if (pState->iFrame - pState->iLastRotate == g_RotationPeriod/4)
	{
		if (pState->Rotation == MOTO_ROTATE_CW)
		{
			int32_t k = min32(g_InvAngPosK, -pState->Bike.AngVel*g_InvMaxDeltaRotV);
			BikeM += g_RotationSpeedSlow_mul_BikeAngularMass*(int64_t)k;
		}
		else
		{
			int32_t k = min32(g_InvAngPosK, pState->Bike.AngVel*g_InvMaxDeltaRotV);
			BikeM -= g_RotationSpeedSlow_mul_BikeAngularMass*(int64_t)k;
		}
	}

	switch (pState->Accel)
	{
	case MOTO_GAS_LEFT:
		/* Accelerate left wheel if player wants it. */
		if (pState->Wheels[0].AngVel > -g_MaxSpeed)
			WheelM[0] = -g_Acceleration;
		break;

	case MOTO_GAS_RIGHT:
		/* Accelerate rightt wheel if player wants it. */
		if (pState->Wheels[1].AngVel < g_MaxSpeed)
			WheelM[1] = g_Acceleration;
		break;

	case MOTO_BRAKE:
		/* Brake both wheels if player wants it. */
		for (int i = 0; i < 2; i++)
		{
			int32_t RelAngVel = pState->Wheels[i].AngVel - pState->Bike.AngVel;
			int32_t k = min32(g_InvAngPosK, abs(RelAngVel)*g_InvMaxBrakingDeltaV);
			WheelM[i] -= sign(RelAngVel)*(int64_t)k*(int64_t)g_Friction;
		}
		break;

	case MOTO_IDLE:
		break;
	}

	/* Axis-aligned unit vector in motocykle coordinate system. */
	int32_t X[2] = { cos16_32(pState->Bike.AngPos >> 16), sin16_32(pState->Bike.AngPos >> 16) };

	/* Intended positions of wheels and head relative to bike. */
	int32_t Wheel0Pos[2] = { -mulsu32(X[0], g_BikePos0) + mulsu32(X[1], g_BikePos1), -mulsu32(X[1], g_BikePos0) - mulsu32(X[0], g_BikePos1) };
	int32_t Wheel1Pos[2] = { mulsu32(X[0], g_BikePos0) + mulsu32(X[1], g_BikePos1), mulsu32(X[1], g_BikePos0) - mulsu32(X[0], g_BikePos1) };
	int32_t HeadPos[2] = { -mulsu32(X[1], g_HeadPos), mulsu32(X[0], g_HeadPos) };

	/* Compute internal bike forces and torques. */
    computeBikeWheelForces(&pState->Bike, &pState->Wheels[0], Wheel0Pos, WheelF[0], BikeF, &BikeM);
    computeBikeWheelForces(&pState->Bike, &pState->Wheels[1], Wheel1Pos, WheelF[1], BikeF, &BikeM);
	computeBikeHeadForces(pState, HeadPos, HeadF);

	/* Handle wheel-ground interaction and integrate wheels positions and velocities. */
	for (int i = 0; i < 2; i++)
		if (!advanceWheel(&pState->Wheels[i], WheelF[i], WheelM[i], pWorld))
		{
			pState->Dead = true;
			return MOTO_FAILURE; /* Wheel is inside of ground. */
		}

	/* Integrate bike position and velocity. */
	pState->Bike.AngPos += pState->Bike.AngVel;
	pState->Bike.AngVel += (int32_t)(BikeM/g_BikeAngularMass_div_dt);
	pState->Bike.Pos[0] += pState->Bike.Vel[0];
	pState->Bike.Pos[1] += pState->Bike.Vel[1];
	pState->Bike.Vel[0] += (int32_t)(BikeF[0]/g_BikeMass_div_dt);
	pState->Bike.Vel[1] += (int32_t)(BikeF[1]/g_BikeMass_div_dt);

	/* Integrate head position and velocity. */
	pState->HeadPos[0] += pState->HeadVel[0];
	pState->HeadPos[1] += pState->HeadVel[1];
	pState->HeadVel[0] += (int32_t)(HeadF[0]/g_HeadMass_div_dt);
	pState->HeadVel[1] += (int32_t)(HeadF[1]/g_HeadMass_div_dt);


//    pState->finishDistSq=min(min(distSq(pState->Wheels[0].Pos, g_MotoFinish),
//            distSq(pState->Wheels[1].Pos, g_MotoFinish)),
//            distSq(pState->HeadPos, g_MotoFinish)
//            );
    pState->finishDistSq=distSqp(pState->Bike.Pos, g_MotoFinish);

    if (isDistLess(pState->Wheels[0].Pos, g_MotoFinish, g_2WheelR_div_PosK) ||
		isDistLess(pState->Wheels[1].Pos, g_MotoFinish, g_2WheelR_div_PosK) ||
        isDistLess(pState->HeadPos, g_MotoFinish, g_Head_plus_WheelR_div_PosK))
		return MOTO_SUCCESS;

	if (getGroundCollideDist65536(pState->HeadPos, pWorld) < g_65536HeadR)
	{
		pState->Dead = true;
		return MOTO_FAILURE;
	}

	return MOTO_CONTINUE;
}

static bool pushInputUpdate(MotoPoW* pPoW, uint16_t Update)
{
    if (pPoW->NumUpdates == MOTO_MAX_INPUTS)
		return false;
	pPoW->Updates[pPoW->NumUpdates] = Update;
	pPoW->NumUpdates++;
	return true;
}

bool recordInput(MotoPoW* pPoW, MotoState* pState, EMotoAccel Accel, EMotoRot Rotation)
{	
	uint16_t PrevUpdate = (pPoW->NumUpdates == 0) ? 0 : pPoW->Updates[pPoW->NumUpdates - 1];
	EMotoAccel PrevAccel = (EMotoAccel)(PrevUpdate % 4);
	if (Accel == PrevAccel && Rotation == MOTO_NO_ROTATION)
		return true;

	int iLastInputFrame = 0;
	for (int i = 0; i < pPoW->NumUpdates; i++)
		iLastInputFrame += pPoW->Updates[i] / 12;

	int16_t iFrameDelta = pState->iFrame - iLastInputFrame;
//    cout << "FrameDelta = pState->iFrame - iLastInputFrame;"<<iFrameDelta<<iLastInputFrame << endl;
	while (iFrameDelta >= 5461)
	{
		if (!pushInputUpdate(pPoW, 5460*12 + (PrevUpdate % 12)))
			return false;
		iFrameDelta -= 5460;
	}

	return pushInputUpdate(pPoW, iFrameDelta*12 + (int16_t)(Rotation*4) + (int16_t)Accel);
}

EMotoResult motoAdvance(MotoState* pState, MotoPoW* pPoW, const MotoWorld* pWorld, EMotoAccel Accel, EMotoRot Rotation, int16_t NumFrames)
{
	if (NumFrames <= 0)
		return MOTO_CONTINUE;

	for (int i = 0; i < NumFrames; i++)
	{
		EMotoRot NewRotation = (pState->iFrame - pState->iLastRotate >= g_RotationPeriod) ? Rotation : MOTO_NO_ROTATION;
		if (!recordInput(pPoW, pState, Accel, NewRotation))
		{
			NewRotation = MOTO_NO_ROTATION;
			Accel = (EMotoAccel)(pPoW->Updates[MOTO_MAX_INPUTS - 1] % 4);
		}
		EMotoResult Result = advanceOneFrame(pState, Accel, NewRotation, pWorld);
		pPoW->NumFrames = pState->iFrame;
		if (Result != MOTO_CONTINUE)
			return Result;
	}
	return MOTO_CONTINUE;
}




bool motoGenerateRandomWorld(MotoWorld* pWorld, MotoState* pState, const uint8_t* pWork, MotoPoW* pow)
{
    int32_t P[2];
	initTables();
    bool t=false;
    int max_t=18,score=0,max_score=0,max_i=0,i,n=0;

    uint8_t BlockPlusNonce[MOTO_WORK_SIZE + 1 + sizeof(uint32_t)];
    memcpy(BlockPlusNonce + 1 + sizeof(uint32_t), pWork, MOTO_WORK_SIZE);

    int offset = -300;

    while(t!=true){
        if(n%100000==0){DEBUG_MSG("n: "<<n);}
        n++;
        i=0;score=0;
        pow->Nonce++;
        memcpy(BlockPlusNonce + 1, &(pow->Nonce), sizeof(uint32_t));

        SHA512(BlockPlusNonce, MOTO_WORK_SIZE + 1 + sizeof(uint32_t), ((uint8_t*)pWorld->Map) +  512/8*7);


        for (i = 0; i < 2*MOTO_MAP_SIZE*MOTO_MAP_SIZE/(512/8); i++)
        {
            BlockPlusNonce[0] = i;
            SHA512(BlockPlusNonce, MOTO_WORK_SIZE + 1 + sizeof(uint32_t), ((uint8_t*)pWorld->Map) +  512/8*i);
//            if(i<7&&i>1&&(pWorld->Map[i*2][5][0]<0||pWorld->Map[i*2][6][0]>0||
//                   pWorld->Map[i*2+1][5][0]<0||pWorld->Map[i*2+1][6][0]>0)){
//                break;
//            }
        }

        for(i=0;i<=max_t;i++){
            P[0]=(g_MotoFinishL[0]/max_t*(max_t-i)+g_MotoStartL[0]/max_t*(i));
            P[1]=((g_MotoFinishL[1])/max_t*(max_t-i)+g_MotoStartL[1]/max_t*(i));
            if(getF(pWorld,P)<-offset){
                score++;
            }
        }
//        if(i>max_i){
//            max_i=i;
//            cout<<"i: "<<i<<endl;
//            cout<<"n: "<<pow->Nonce<<endl;
//        }
        if(score>max_score){
            max_score=score;
            DEBUG_MSG("score: "<<score);
            DEBUG_MSG("nonce: "<<pow->Nonce);
        }
        if(score>max_t){break;}
    }
	for (int i = 0; i < MOTO_MAP_SIZE; i++)
	{
		pWorld->Map[i][0][0] = 0;
		pWorld->Map[i][0][1] = 127;
		//pWorld->Map[i][MOTO_MAP_SIZE-1][0] = 0;
		//pWorld->Map[i][MOTO_MAP_SIZE-1][1] = -127;
	}

	memset(pState, 0, sizeof(MotoState));
	pState->iLastRotate = -10000;
	pState->Wheels[0].Pos[0] = g_MotoStart[0];
	pState->Wheels[0].Pos[1] = g_MotoStart[1];
	pState->Wheels[1].Pos[0] = pState->Wheels[0].Pos[0] + g_WheelDist;
	pState->Wheels[1].Pos[1] = pState->Wheels[0].Pos[1];
	pState->Bike.Pos[0] = pState->Wheels[0].Pos[0] + g_BikePos0;
	pState->Bike.Pos[1] = pState->Wheels[0].Pos[1] + g_BikePos1;
	pState->HeadPos[0] = pState->Bike.Pos[0];
	pState->HeadPos[1] = pState->Bike.Pos[1] + g_HeadPos;

	/* Check that finish is not inside of rock and also test one frame to see if something is wrong. */
    MotoState TestFrame = *pState;
    bool goodFin = getGroundCollideDist65536(g_MotoFinish, pWorld) > g_65536WheelR ;
    bool goodStart = advanceOneFrame(&TestFrame, MOTO_IDLE, MOTO_NO_ROTATION, pWorld) == MOTO_CONTINUE;
    bool res = goodFin&&goodStart ;
    if(!goodFin){
        DEBUG_MSG("ill formed fin");
    }
    if(!goodStart){
        DEBUG_MSG("ill formed start");
    }
    return res;
}

bool motoGenerateGoodWorld(MotoWorld* pWorld, MotoState* pState, const uint8_t* pWork, MotoPoW* pow){
//    return motoGenerateWorld(pWorld, pState, pWork, pow->Nonce);
    return motoGenerateRandomWorld(pWorld, pState, pWork, pow);
    int bestWorld=0;
    int64_t bestScore=0,score=0;
    int32_t tmpVec[2];
    int max_t=300;
    int i=0;
    while(bestScore<210){
        i++;
        pow->Nonce++;
        if(!motoGenerateRandomWorld(pWorld, pState, pWork, pow)){
            continue;
        }
        for(int32_t t=0;t<max_t;t++){
//            tmpVec[1]=g_MotoFinish[1]+(g_MotoStart[1]-g_MotoFinish[1])/100*t/100*t;
//            tmpVec[0]=(g_MotoFinish[0]/max_t*(max_t-t)+g_MotoStart[0]/max_t*t);
            tmpVec[0]=(g_MotoFinish[0]/max_t*t+g_MotoStart[0]/max_t*(max_t-t));
            tmpVec[1]=(g_MotoFinish[1]/max_t*t+g_MotoStart[1]/max_t*(max_t-t));
            int dist=getGroundCollideDist65536(tmpVec, pWorld);
            score+=dist>g_65536WheelR*3?1:0;
            //score+=dist;
        }
        if(score>bestScore){
            bestScore=score;
            bestWorld=i;
//            cout<<"score:"<<bestScore<<endl;
        }
        score=0;

    }

    motoGenerateRandomWorld(pWorld, pState, pWork, pow);
    return true;
}

bool motoGenerateWorld(MotoWorld* pWorld, MotoState* pState, const uint8_t* pWork, uint32_t Nonce)
{
initTables();

uint8_t BlockPlusNonce[MOTO_WORK_SIZE + 1 + sizeof(uint32_t)];
memcpy(BlockPlusNonce + 1, &Nonce, sizeof(uint32_t));
memcpy(BlockPlusNonce + 1 + sizeof(uint32_t), pWork, MOTO_WORK_SIZE);

for (int i = 0; i < 2*MOTO_MAP_SIZE*MOTO_MAP_SIZE/(512/8); i++)
{
BlockPlusNonce[0] = i;
        SHA512(BlockPlusNonce, MOTO_WORK_SIZE + 1 + sizeof(uint32_t), ((uint8_t*)pWorld->Map) + 512/8*i);
}
for (int i = 0; i < MOTO_MAP_SIZE; i++)
{
pWorld->Map[i][0][0] = 0;
pWorld->Map[i][0][1] = 127;
//pWorld->Map[i][MOTO_MAP_SIZE-1][0] = 0;
//pWorld->Map[i][MOTO_MAP_SIZE-1][1] = -127;
}

memset(pState, 0, sizeof(MotoState));
pState->iLastRotate = -10000;
pState->Wheels[0].Pos[0] = g_MotoStart[0];
pState->Wheels[0].Pos[1] = g_MotoStart[1];
pState->Wheels[1].Pos[0] = pState->Wheels[0].Pos[0] + g_WheelDist;
pState->Wheels[1].Pos[1] = pState->Wheels[0].Pos[1];
pState->Bike.Pos[0] = pState->Wheels[0].Pos[0] + g_BikePos0;
pState->Bike.Pos[1] = pState->Wheels[0].Pos[1] + g_BikePos1;
pState->HeadPos[0] = pState->Bike.Pos[0];
pState->HeadPos[1] = pState->Bike.Pos[1] + g_HeadPos;

/* Check that finish is not inside of rock and also test one frame to see if something is wrong. */
MotoState TestFrame = *pState;
return getGroundCollideDist65536(g_MotoFinish, pWorld) > g_65536WheelR && advanceOneFrame(&TestFrame, MOTO_IDLE, MOTO_NO_ROTATION, pWorld) == MOTO_CONTINUE;
}

bool motoCheck(const uint8_t* pWork, MotoPoW* pPoW)
{
	if (pPoW->NumUpdates > MOTO_MAX_INPUTS)
		return false;

	MotoWorld World;
	MotoState State;
	motoGenerateWorld(&World, &State, pWork, pPoW->Nonce);
	return motoReplay(&State, pPoW, &World, MOTO_MAX_FRAMES + 10);
}

bool motoReplay(MotoState* pState, MotoPoW* pPoW, const MotoWorld* pWorld, int16_t iToFrame)
{
	int16_t iFrame = 0;
	EMotoAccel Accel = MOTO_IDLE;
	EMotoRot Rotation = MOTO_NO_ROTATION;
	for (unsigned int i = 0; i <= pPoW->NumUpdates; i++)
	{
		if (i == pPoW->NumUpdates)
			iFrame = pPoW->NumFrames;
		else
		{
			uint16_t iFrameDelta = pPoW->Updates[i] / 12;
			iFrame += iFrameDelta;
			if (iFrame >= pPoW->NumFrames)
				return false;

			uint16_t Update = pPoW->Updates[i];
			EMotoAccel NewAccel = (EMotoAccel)(Update % 4);
			EMotoRot NewRotation = (EMotoRot)((Update % 12) / 4);
            if (iFrameDelta != 5460 && NewAccel == Accel && NewRotation == MOTO_NO_ROTATION){ /* This update is useless. */
//                cout << "useless"<<iFrameDelta<<" i:"<<i <<endl;
                return false;
            }
		}
		while (pState->iFrame < iFrame)
		{
            pState->curState=advanceOneFrame(pState, Accel, Rotation, pWorld);
            switch (pState->curState)
			{
            case MOTO_CONTINUE:
//                cout << "continue" <<endl;
				break;

			case MOTO_SUCCESS:
//                cout << "success" <<endl;
                pPoW->NumFrames=pState->iFrame;
                pPoW->NumUpdates=i;
				return pState->iFrame == pPoW->NumFrames && i == pPoW->NumUpdates;

			case MOTO_FAILURE:
                //cout << "failure" <<endl;
				return false;
			}
			Rotation = MOTO_NO_ROTATION;

            if (pState->iFrame >= iToFrame){
//                cout << "(pState->iFrame >= iToFrame)" <<endl;
				return false;
            }
		}

		Accel = (EMotoAccel)(pPoW->Updates[i] % 4);
		Rotation = (EMotoRot)((pPoW->Updates[i] / 4) % 3);
	}
//    cout << "end" <<endl;
	return false;
}

void motoCutPoW(MotoPoW* pPoW, int16_t iToFrame)
{
	pPoW->NumFrames = iToFrame;
	int16_t iFrame = 0;
	for (unsigned int i = 0; i < pPoW->NumUpdates; i++)
	{
		uint16_t iFrameDelta = pPoW->Updates[i] / 12;
		iFrame += iFrameDelta;
		if (iFrame >= iToFrame)
		{
			pPoW->NumUpdates = i;
			return;
		}
	}
}

void motoInitPoW(MotoPoW* pPoW)
{
	memset(pPoW, 0, sizeof(MotoPoW));
}
