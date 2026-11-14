/**
 * @file WisBlockPassword.cpp
 * @brief Password hashing and protection state, see WisBlockPassword.h
 */
#include "WisBlockPassword.h"
#include <string.h>

namespace WisBlockPassword
{
namespace
{
const uint32_t kK[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n)
{
	return (x >> n) | (x << (32 - n));
}

struct Sha256Ctx
{
	uint32_t h[8];
	uint8_t buf[64];
	uint64_t totalBytes;
	size_t fill;
};

void sha256Block(Sha256Ctx &c, const uint8_t *p)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
	{
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
	}
	for (int i = 16; i < 64; i++)
	{
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3], e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
	for (int i = 0; i < 64; i++)
	{
		uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = h + S1 + ch + kK[i] + w[i];
		uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
		uint32_t t2 = S0 + maj;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = cc;
		cc = b;
		b = a;
		a = t1 + t2;
	}
	c.h[0] += a;
	c.h[1] += b;
	c.h[2] += cc;
	c.h[3] += d;
	c.h[4] += e;
	c.h[5] += f;
	c.h[6] += g;
	c.h[7] += h;
}

void sha256Init(Sha256Ctx &c)
{
	static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
									 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(c.h, init, sizeof(init));
	c.totalBytes = 0;
	c.fill = 0;
}

void sha256Update(Sha256Ctx &c, const uint8_t *data, size_t len)
{
	c.totalBytes += len;
	while (len > 0)
	{
		size_t n = 64 - c.fill;
		if (n > len)
		{
			n = len;
		}
		memcpy(c.buf + c.fill, data, n);
		c.fill += n;
		data += n;
		len -= n;
		if (c.fill == 64)
		{
			sha256Block(c, c.buf);
			c.fill = 0;
		}
	}
}

void sha256Final(Sha256Ctx &c, uint8_t out[32])
{
	uint64_t bits = c.totalBytes * 8;
	uint8_t pad = 0x80;
	sha256Update(c, &pad, 1);
	uint8_t zero = 0;
	while (c.fill != 56)
	{
		sha256Update(c, &zero, 1);
	}
	uint8_t lenBytes[8];
	for (int i = 0; i < 8; i++)
	{
		lenBytes[i] = (uint8_t)(bits >> (56 - 8 * i));
	}
	sha256Update(c, lenBytes, 8);
	for (int i = 0; i < 8; i++)
	{
		out[i * 4] = (uint8_t)(c.h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(c.h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(c.h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)c.h[i];
	}
}

const uint8_t kRecordVersion = 1;
const int kRounds = 1000;

// h = SHA256(salt || pw), then kRounds - 1 times h = SHA256(h || salt || pw)
void derive(const char *pw, const uint8_t salt[8], uint8_t out[32])
{
	size_t pwLen = strlen(pw);
	Sha256Ctx c;
	sha256Init(c);
	sha256Update(c, salt, 8);
	sha256Update(c, (const uint8_t *)pw, pwLen);
	sha256Final(c, out);
	for (int i = 1; i < kRounds; i++)
	{
		sha256Init(c);
		sha256Update(c, out, 32);
		sha256Update(c, salt, 8);
		sha256Update(c, (const uint8_t *)pw, pwLen);
		sha256Final(c, out);
	}
}

// Compares without stopping at the first difference
bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t len)
{
	uint8_t diff = 0;
	for (size_t i = 0; i < len; i++)
	{
		diff |= (uint8_t)(a[i] ^ b[i]);
	}
	return diff == 0;
}
} // namespace

bool isValidPassword(const char *pw)
{
	if (!pw)
	{
		return false;
	}
	size_t len = strlen(pw);
	if (len < kMinLength || len > kMaxLength)
	{
		return false;
	}
	for (size_t i = 0; i < len; i++)
	{
		unsigned char ch = (unsigned char)pw[i];
		if (ch < 0x21 || ch > 0x7E)
		{
			return false;
		}
	}
	return true;
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
	Sha256Ctx c;
	sha256Init(c);
	sha256Update(c, data, len);
	sha256Final(c, out);
}

void makeRecord(const char *pw, const uint8_t salt[8], uint8_t record[kRecordSize])
{
	record[0] = kRecordVersion;
	memcpy(record + 1, salt, 8);
	derive(pw, salt, record + 9);
}

bool verifyRecord(const char *pw, const uint8_t record[kRecordSize])
{
	if (!pw || record[0] != kRecordVersion)
	{
		return false;
	}
	uint8_t hash[32];
	derive(pw, record + 1, hash);
	return constantTimeEqual(hash, record + 9, 32);
}

void Guard::load()
{
	if (loaded)
	{
		return;
	}
	loaded = true;
	hasPassword = store.read(record) && record[0] == kRecordVersion;
	uint8_t stored = 0;
	if (store.readFailures(stored))
	{
		failures = stored > kMaxFailures ? kMaxFailures : stored;
		storedFailures = failures;
		if (failures >= kMaxFailures)
		{
			// Counted before the restart: no attempt in the first kBlockMs after the start of the device
			blocked = true;
			blockedSinceMs = 0;
		}
	}
}

// Keeps the number of wrong passwords, written only when it changes: at most kMaxFailures times in a row of
// wrong passwords, and once when the right password resets it
void Guard::setFailures(uint8_t count)
{
	failures = count;
	if (count != storedFailures && store.writeFailures(count))
	{
		storedFailures = count;
	}
}

bool Guard::enabled()
{
	load();
	return hasPassword;
}

Result Guard::set(const char *pw, const uint8_t salt[8])
{
	load();
	if (hasPassword && !isUnlocked)
	{
		return Result::Denied;
	}
	if (!isValidPassword(pw))
	{
		return Result::Invalid;
	}
	uint8_t newRecord[kRecordSize];
	makeRecord(pw, salt, newRecord);
	if (!store.write(newRecord))
	{
		return Result::Error;
	}
	memcpy(record, newRecord, kRecordSize);
	hasPassword = true;
	isUnlocked = false; // the new password must be sent before reading
	setFailures(0);
	blocked = false;
	return Result::Ok;
}

Result Guard::remove()
{
	load();
	if (!hasPassword)
	{
		return Result::NoPassword;
	}
	if (!isUnlocked)
	{
		return Result::Denied;
	}
	if (!store.erase())
	{
		return Result::Error;
	}
	hasPassword = false;
	isUnlocked = false;
	setFailures(0);
	blocked = false;
	return Result::Ok;
}

Result Guard::unlock(const char *pw, uint32_t nowMs)
{
	load();
	if (!hasPassword)
	{
		return Result::Ok; // protection is off, nothing to unlock
	}
	if (blocked)
	{
		// Elapsed time as unsigned: right across the wrap of millis(), and a pause nobody asked about for weeks is over
		if ((uint32_t)(nowMs - blockedSinceMs) < kBlockMs)
		{
			return Result::Blocked;
		}
		// The pause is over. The counter stays, so one more wrong password blocks again
		blocked = false;
	}
	if (verifyRecord(pw, record))
	{
		isUnlocked = true;
		setFailures(0);
		return Result::Ok;
	}
	isUnlocked = false;
	if (failures < kMaxFailures)
	{
		setFailures(failures + 1);
	}
	if (failures >= kMaxFailures)
	{
		blocked = true;
		blockedSinceMs = nowMs;
	}
	return Result::Denied;
}
} // namespace WisBlockPassword
