/**
 * @file LoRaWANEngine.h
 * @brief Wraps Semtech LoRa Basics Modem v4.9.0's `smtc_modem_api` (vendored
 * at src/lbm/smtc_modem_api/) for join, uplink, class switching, ADR, relay,
 * link check and device-time request.
 *
 * Every public method here maps to real, verified `smtc_modem_api` /
 * `smtc_modem_utilities` calls - see LoRaWANEngine.cpp for the exact
 * function names and any real API limitations discovered while wiring this
 * up (e.g. v4.9.0 has no direct "set TX power" call; see setTxPower()).
 */
#ifndef LORAWAN_ENGINE_H
#define LORAWAN_ENGINE_H

#include "LoRaWANRelay.h"
#include "WisBlockLoRaWANTypes.h"

class LoRaWANEngine
{
public:
	using JoinSuccessCb = void (*)();
	using JoinFailedCb = void (*)();
	using TxFinishedCb = void (*)(const WisBlockTxResult &);
	using RxFinishedCb = void (*)(const WisBlockRxResult &);
	using TimeRequestCb = void (*)(bool success, const WisBlockTimeAnswer &);
	using LinkCheckCb = void (*)(bool success, const WisBlockLinkCheckResult &);

	void begin(const WisBlockLoRaWANSettings &settings);
	void applySettings(const WisBlockLoRaWANSettings &settings);

	void join();
	bool isJoined() const;
	WisBlockJoinState joinState() const;

	/**
	 * Queues an uplink with LBM. Unlike a bare pass-through to
	 * smtc_modem_request_uplink(), this refuses (returns false, no LBM call
	 * made at all) if a previous send() is still in flight - LBM holds at
	 * most one pending uplink, and calling this again before the previous
	 * one has actually been dispatched silently discards it (still
	 * reported honestly afterward via onTxFinished() with success=false
	 * and SMTC_MODEM_EVENT_TXDONE_NOT_SENT - that part of LBM's behavior
	 * was always correct; nothing was being mis-reported). See the
	 * "First send after join lost" / uplinkPending README notes for the
	 * real log capture that prompted this - a full Class A TX+RX1+RX2
	 * cycle, especially amid post-join MAC command negotiation, routinely
	 * took longer than a naive fixed-interval application timer expected,
	 * so periodic sends were racing (and losing to) the send already in
	 * flight, wasting a frame counter each time.
	 */
	bool send(uint8_t port, const uint8_t *data, uint8_t length, bool confirmed);

	void setDeviceClass(WisBlockDeviceClass deviceClass);
	void setADR(bool enabled);
	void setTxPower(uint8_t txPowerIndex);
	void setDataRate(uint8_t dataRate);

	/** Requests link check; answer arrives later as an SMTC_MODEM_EVENT_LINK_CHECK event. */
	void requestLinkCheck();
	/**
	 * Pull-style alternative to onLinkCheckAnswer(): fetches the most
	 * recently received link check answer directly from LBM
	 * (smtc_modem_get_lorawan_link_check_data), rather than waiting for the
	 * push callback. Returns false if no link check has ever been answered
	 * yet (LBM has nothing cached to return). Safe to call at any time,
	 * not just right after a request - the value stays cached until the
	 * next successful link check answer overwrites it.
	 */
	bool getLinkCheckResult(WisBlockLinkCheckResult &out) const;
	/** Requests device time; answer arrives later as an SMTC_MODEM_EVENT_LORAWAN_MAC_TIME event. */
	void requestDeviceTime();

	void setRelayMode(WisBlockRelayMode mode); // see LoRaWANRelay.h
	void configureRelayED(const WisBlockRelayEDConfig &cfg);
	void configureRelayServing(const WisBlockRelayServingConfig &cfg);
	bool addRelayTrustedDevice(const WisBlockRelayTrustedDevice &device);
	bool removeRelayTrustedDevice(uint8_t index);

	/** Pumps smtc_modem_run_engine() + drains smtc_modem_get_event(). Call every loop().
	 * Returns the ms budget smtc_modem_run_engine() itself reports before it must be
	 * called again - required for background task mode (WisBlockLbmTask) to self-schedule
	 * its next wake; safe to ignore in loop()-polled usage. */
	uint32_t handleEvents();

	void onJoinSuccess(JoinSuccessCb cb) { joinSuccessCb = cb; }
	void onJoinFailed(JoinFailedCb cb) { joinFailedCb = cb; }
	void onTxFinished(TxFinishedCb cb) { txFinishedCb = cb; }
	void onRxFinished(RxFinishedCb cb) { rxFinishedCb = cb; }
	void onTimeRequestAnswer(TimeRequestCb cb) { timeRequestCb = cb; }
	void onLinkCheckAnswer(LinkCheckCb cb) { linkCheckCb = cb; }

private:
	WisBlockLoRaWANSettings settings;
	WisBlockJoinState currentJoinState = WISBLOCK_JOIN_IDLE;
	// FIX: see send()'s doc comment - tracks whether an uplink is currently
	// queued/in-flight with LBM. Set true the moment send() successfully
	// calls smtc_modem_request_uplink(); cleared unconditionally the
	// moment SMTC_MODEM_EVENT_TXDONE fires (success or not - either way
	// LBM is no longer holding a pending uplink for us afterward). Reset
	// to false in begin(), since a fresh smtc_modem_init() starts with
	// nothing pending regardless of whatever this flag happened to be
	// left at from a previous run.
	bool uplinkPending = false;

	JoinSuccessCb joinSuccessCb = nullptr;
	JoinFailedCb joinFailedCb = nullptr;
	TxFinishedCb txFinishedCb = nullptr;
	RxFinishedCb rxFinishedCb = nullptr;
	TimeRequestCb timeRequestCb = nullptr;
	LinkCheckCb linkCheckCb = nullptr;

	void applyAdrProfile(); // builds the custom dr_custom_distribution_data table when ADR is off
};

#endif // LORAWAN_ENGINE_H
