#pragma once

#include <stdint.h>

__device__ __forceinline__ uint32_t get_reciprocal(const uint32_t a)
{
	const float a_hi = __uint_as_float((a >> 8) + ((126U + 31U) << 23));
	const float a_lo = __uint2float_rn(a & 0xFF);

	float r;
	asm("rcp.f32.rn %0, %1;" : "=f"(r) : "f"(a_hi));

	const float r_scaled = __uint_as_float(__float_as_uint(r) + (64U << 23));

	const float h = __fmaf_rn(a_lo, r, __fmaf_rn(a_hi, r, -1.0f));
	return (__float_as_uint(r) << 9) - __float2int_rn(h * r_scaled);
}

__device__ __forceinline__ uint64_t get_reciprocal64(uint32_t a)
{
	asm("// get_reciprocal64 BEGIN");
	const uint32_t shift = __clz(a);
	a <<= shift;

	const float a_hi = __uint_as_float((a >> 8) + 1 + ((126U + 31U) << 23));
	const float a_lo = __int2float_rn((a & 0xFF) - 256);

	float r;
	asm("rcp.approx.f32 %0, %1;" : "=f"(r) : "f"(a_hi));

	const uint32_t tmp0 = __float_as_uint(r);
	const uint32_t tmp1 = tmp0 + ((shift + 2 + 64U) << 23);
	const float r_scaled = __uint_as_float(tmp1);

	const float h = __fmaf_rn(a_lo, r, __fmaf_rn(a_hi, r, -1.0f));

	const float r_scaled_hi = __uint_as_float(tmp1 & ~uint32_t(4095));
	const float h_hi = __uint_as_float(__float_as_uint(h) & ~uint32_t(4095));

	const float r_scaled_lo = r_scaled - r_scaled_hi;
	const float h_lo = h - h_hi;

	const float x1 = h_hi * r_scaled_hi;
	const float x2 = __fmaf_rn(h_lo, r_scaled, h_hi * r_scaled_lo);

	const int64_t h1 = __float2ll_rn(x1);
	const int32_t h2 = __float2int_ru(x2) - __float2int_rd(h * (x1 + x2));

	const uint64_t result = (uint64_t(tmp0 & 0xFFFFFF) << (shift + 9)) - ((h1 + h2) >> 2);
	asm("// get_reciprocal64 END");

	return result;
}

__device__ __forceinline__ uint64_t fast_div_v2(const uint64_t a, const uint32_t b)
{
	const uint32_t r = get_reciprocal(b);
	const uint64_t k = __umulhi(((uint32_t*)&a)[0], r) + ((uint64_t)(r) * ((uint32_t*)&a)[1]) + a;

	uint32_t q[2];
	q[0] = ((uint32_t*)&k)[1];

	int64_t tmp = a - (uint64_t)(q[0]) * b;
	((int32_t*)(&tmp))[1] -= (k < a) ? b : 0;

	const int8_t overshoot = ((int32_t*)(&tmp))[1] >> 31;
	const int8_t undershoot = (tmp - b) >> 63;

	q[0] += 1 + undershoot + overshoot;
	q[1] = ((uint32_t*)(&tmp))[0] + (b & overshoot) + ((b & undershoot) - b);

	return *((uint64_t*)(q));
}

__device__ __forceinline__ uint32_t fast_sqrt_v2(const uint64_t n1)
{
	float x = __uint_as_float((((uint32_t*)&n1)[1] >> 9) + ((64U + 127U) << 23));
	float x1;
	asm("rsqrt.approx.f32 %0, %1;" : "=f"(x1) : "f"(x));
	asm("rcp.approx.f32 %0, %1;" : "=f"(x) : "f"(x1));

	// The following line does x1 *= 4294967296.0f;
	x1 = __uint_as_float(__float_as_uint(x1) + (32U << 23));

	const uint32_t x0 = __float_as_uint(x) - (158U << 23);
	const int64_t delta0 = n1 - (((int64_t)(x0) * x0) << 18);
	const float delta = __int2float_rn(((int32_t*)&delta0)[1]) * x1;

	uint32_t result = (x0 << 10) + __float2int_rn(delta);
	const uint32_t s = result >> 1;
	const uint32_t b = result & 1;

	const uint64_t x2 = (uint64_t)(s) * (s + b) + ((uint64_t)(result) << 32) - n1;
	result -= (((int64_t)(x2 + b) >> 63) + 1) + ((int64_t)(x2 + 0x100000000UL + s) >> 63);

	return result;
}

__device__ __forceinline__ uint64_t fast_div_heavy_old(int64_t _a, int32_t _b)
{
	const uint64_t a = abs(_a);
	const uint32_t b = abs(_b);
	uint64_t q = __umul64hi(a, get_reciprocal64(b));

	const int64_t tmp = a - q * b;
	const int32_t overshoot = (tmp < 0) ? 1 : 0;
	const int32_t undershoot = (tmp >= b) ? 1 : 0;
	q += undershoot - overshoot;

	return ((((int32_t*)&_a)[1] ^ _b) < 0) ? -q : q;
}

__device__ __forceinline__ uint64_t fast_div_heavy(int64_t _a, int32_t _b)
{
	int64_t a = abs(_a);
	int32_t b = abs(_b);

	float rcp;
	asm("rcp.approx.f32 %0, %1;" : "=f"(rcp) : "f"(__int2float_rn(b)));
	float rcp2 = __uint_as_float(__float_as_uint(rcp) + (32U << 23));

	uint64_t q1 = __float2ull_rd(__int2float_rn(((int32_t*)&a)[1]) * rcp2);
	a -= q1 * (uint32_t)(b);

	rcp2 = __uint_as_float(__float_as_uint(rcp) + (12U << 23));
	int64_t q2 = __float2ll_rn(__int2float_rn(a >> 12) * rcp2);
	int32_t a2 = ((int32_t*)&a)[0] - ((int32_t*)&q2)[0] * b;

	int32_t q3 = __float2int_rn(__int2float_rn(a2) * rcp);
	q3 += (a2 - q3 * b) >> 31;

	const int64_t q = q1 + q2 + q3;
	return ((((int32_t*)&_a)[1] ^ _b) < 0) ? -q : q;
}
