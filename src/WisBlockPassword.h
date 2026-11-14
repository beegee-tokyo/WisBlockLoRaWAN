/**
 * @file WisBlockPassword.h
 * @brief Optional password protection for reading the LoRaWAN credentials and keys with AT commands
 *
 * @details Pure C++ (no Arduino, no flash access), so the logic can be tested on a PC. The storage is
 * given to the Guard through a small interface, WisBlockLoRaAT.cpp implements it with WisBlockLoRaFlash.
 *
 * Behavior:
 *  - By default no password is set and the protection is off: everything can be read.
 *  - Setting a password turns the protection on and locks (the password must be sent before the
 *    protected values can be read).
 *  - Sending the correct password unlocks until lock() or a restart of the device (the unlocked state is
 *    only in RAM).
 *  - Changing or deleting the password needs the unlocked state while a password is set.
 *  - Deleting the password turns the protection off. It stays off, also after a restart, until a password
 *    is set again.
 *  - After kMaxFailures wrong passwords in a row, unlock() refuses every attempt for kBlockMs, then allows one
 *    attempt, and refuses again for kBlockMs after a wrong one. The number of wrong passwords is kept in the
 *    storage until the right password is sent, so a reset or a power cycle does not clear it: after a restart
 *    the first attempt is possible kBlockMs after the start of the device.
 *
 * What it is not: the password is stored as a salted SHA-256 hash (1000 rounds), but the keys themselves
 * are still stored unencrypted in the flash, and the password travels unencrypted over the serial port.
 * It keeps people with access to the AT interface from reading the keys, nothing more.
 */
#ifndef WISBLOCK_PASSWORD_H
#define WISBLOCK_PASSWORD_H

#include <stddef.h>
#include <stdint.h>

namespace WisBlockPassword
{
/** Shortest and longest accepted password (characters) */
const size_t kMinLength = 4;
const size_t kMaxLength = 32;
/** Wrong passwords in a row before unlock() is refused for a while */
const uint8_t kMaxFailures = 5;
/** Time unlock() is refused after kMaxFailures wrong passwords, and after the start of the device if that many
 *  wrong passwords were counted before (ms) */
const uint32_t kBlockMs = 30000;
/** Size of the stored record: version (1), salt (8), hash (32) */
const size_t kRecordSize = 41;

/** Result of the Guard operations */
enum class Result : uint8_t
{
	Ok,		   /** Done */
	Denied,	   /** Wrong password, or the unlocked state is needed first */
	Blocked,   /** Too many wrong passwords, try again later */
	Invalid,   /** Password not accepted (length or characters) */
	NoPassword, /** There is no password set */
	Error	   /** Storage failed */
};

/**
 * @brief Check a password for the allowed length and characters
 *
 * @param pw Password, zero terminated
 * @return true if it is 4 to 32 printable ASCII characters without space
 */
bool isValidPassword(const char *pw);

/**
 * @brief SHA-256
 *
 * @param data Input
 * @param len Number of bytes
 * @param out Receives the 32 byte digest
 */
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/**
 * @brief Build the stored record (salted, iterated SHA-256) for a password
 *
 * @param pw Password, zero terminated
 * @param salt 8 bytes of salt
 * @param record Receives kRecordSize bytes
 */
void makeRecord(const char *pw, const uint8_t salt[8], uint8_t record[kRecordSize]);

/**
 * @brief Compare a password with a stored record, in constant time
 *
 * @param pw Password, zero terminated
 * @param record Stored record
 * @return true if the password matches
 */
bool verifyRecord(const char *pw, const uint8_t record[kRecordSize]);

/** Where the record is kept */
class Storage
{
  public:
	virtual ~Storage() {}
	/** Read the record. @return true if one is stored (kRecordSize bytes) */
	virtual bool read(uint8_t record[kRecordSize]) = 0;
	/** Write the record. @return true on success */
	virtual bool write(const uint8_t record[kRecordSize]) = 0;
	/** Delete the record. @return true on success (also if there was none) */
	virtual bool erase() = 0;
	/** Read the number of wrong passwords counted. @return true if a value is stored */
	virtual bool readFailures(uint8_t &count) = 0;
	/** Store the number of wrong passwords counted (0 .. kMaxFailures). @return true on success */
	virtual bool writeFailures(uint8_t count) = 0;
};

/** The protection state: password set or not, locked or unlocked */
class Guard
{
  public:
	explicit Guard(Storage &storage) : store(storage) {}

	/** @return true if a password is set (the protection is on) */
	bool enabled();
	/** @return true if the password was sent since the last lock() or restart */
	bool unlocked() const { return isUnlocked; }
	/** @return true if the protected values may be read (no password set, or unlocked) */
	bool canRead() { return !enabled() || isUnlocked; }

	/**
	 * @brief Set or change the password, this turns the protection on and locks
	 *
	 * @param pw New password
	 * @param salt 8 bytes of salt, for example from the unique ID of the chip
	 * @return Ok, Denied (a password is set and the state is locked), Invalid or Error
	 */
	Result set(const char *pw, const uint8_t salt[8]);

	/**
	 * @brief Delete the password, this turns the protection off
	 *
	 * @return Ok, NoPassword, Denied (locked) or Error
	 */
	Result remove();

	/**
	 * @brief Send the password
	 *
	 * @param pw Password
	 * @param nowMs Time in ms since the start of the device (millis()), for the pause after wrong passwords
	 * @return Ok (also if no password is set), Denied (wrong password) or Blocked
	 */
	Result unlock(const char *pw, uint32_t nowMs);

	/** Back to the locked state */
	void lock() { isUnlocked = false; }

  private:
	void load();
	void setFailures(uint8_t count);

	Storage &store;
	bool loaded = false;
	bool hasPassword = false;
	bool isUnlocked = false;
	uint8_t record[kRecordSize];
	uint8_t failures = 0;		   // wrong passwords counted, at most kMaxFailures
	uint8_t storedFailures = 0;	   // the value in the storage
	bool blocked = false;
	uint32_t blockedSinceMs = 0;   // start of the pause (ms since the start of the device)
};
} // namespace WisBlockPassword

#endif // WISBLOCK_PASSWORD_H
