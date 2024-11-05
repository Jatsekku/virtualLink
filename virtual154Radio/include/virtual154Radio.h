#pragma once 

#include <stdbool.h>
#include <stdint.h>
#include "virtualLink.h"

#define VIRTUAL_154_RADIO_EXTENDED_ADDRESS_SIZE 8

enum virtual154RadioTxStatusCode {
	VIRTUAL_154_RADIO_TX_STATUS_CODE_OK = 0,
	VIRTUAL_154_RADIO_TX_STATUS_CODE_CSMA_FAILED = -1,
	VIRTUAL_154_RADIO_TX_STATUS_CODE_TX_ABORTED = -2,
	VIRTUAL_154_RADIO_TX_STATUS_CODE_GENERIC_ERROR = -3,
};

typedef void virtual154RadioTxDoneCallbackFunction(enum virtual154RadioTxStatusCode status,
						   void *user_data);

enum virtual154RadioRxStatusCode {
	VIRTUAL_154_RADIO_RX_STATUS_CODE_OK = 0,
	VIRTUAL_154_RADIO_RX_STATUS_CODE_NO_MEMORY = -1,
};

typedef void virtual154RadioRxDoneCallbackFunction(enum virtual154RadioRxStatusCode status,
						   const void *const mpdu, uint8_t mpdu_size,
						   void *user_data);

enum virtual154RadioAckStatusCode {
	VIRTUAL_154_RADIO_ACK_STATUS_CODE_OK = 0,
	VIRTUAL_154_RADIO_ACK_STATUS_CODE_TIMEOUT = -1,
};

typedef void virtual154RadioAckDoneCallbackFunction(enum virtual154RadioAckStatusCode status,
						    const void *const mpdu, uint8_t mpdu_size,
						    void *user_data);

enum virtual154RadioState {
	VIRTUAL_154_RADIO_STATE_INVALID = -1,
	VIRTUAL_154_RADIO_STATE_DISABLED = 0,
	VIRTUAL_154_RADIO_STATE_SLEEP = 1,
	VIRTUAL_154_RADIO_STATE_RX = 2,
	VIRTUAL_154_RADIO_STATE_TX = 3,
};

struct virtual154RadioMetaConfig {
	int8_t rx_sensitivity_dbm;
	uint8_t eui64_address[8];
	struct virtualLinkObject *virtual_link;
	uint32_t bus_speed; //unused for now
	bool skip_shr_and_phr;
};

struct virtual154RadioMetaData {
	int8_t rssi_dbm;
};

struct virtual154RadioObject {
	struct virtual154RadioMetaConfig _meta_config;
	struct virtual154RadioMetaData _meta_data;

	uint16_t _pan_id;
	uint16_t _short_address;
	uint8_t _extended_address[VIRTUAL_154_RADIO_EXTENDED_ADDRESS_SIZE];
	int8_t _tx_power_dbm;
	int8_t _cca_ed_threshold_dbm;
	int8_t _lna_gain_dbm;
	bool _promiscuous_mode_state;
	uint8_t _channel;
	enum virtual154RadioState _state;

	struct {
		bool is_expected;
		uint8_t sequence_number;
		uint64_t start_timestamp_us;
		uint64_t timeout;
	} _ack_data;

	struct {
		virtual154RadioAckDoneCallbackFunction *function;
		void *user_data;
	} _ack_done_callback;


	struct {
		virtual154RadioTxDoneCallbackFunction *function;
		void *user_data;
	} _tx_done_callback;

	struct {
		virtual154RadioRxDoneCallbackFunction *function;
		void *user_data;
	} _rx_done_callback;

	bool _is_initialized;
};

/* ----------------------------------------- Meta API ------------------------------------------ */
void virtual154Radio_Meta_init(struct virtual154RadioObject *const object,
			       const struct virtual154RadioMetaConfig *const meta_config);

void virtual154Radio_Meta_setRxSensitivity(struct virtual154RadioObject *const object,
					   int8_t rx_sensitivity_dbm);

int8_t virtual154Radio_Meta_getRxSensitivity(const struct virtual154RadioObject *const object);


void virtual154Radio_Meta_setEui64Address(struct virtual154RadioObject *const object,
					  const uint8_t *const eui64_address);

void virtual154Radio_Meta_getEui64Address(const struct virtual154RadioObject *const object,
					  uint8_t *const eui64_address);

void virtual154Radio_Meta_processingLoop(struct virtual154RadioObject *const object);

void virtual154Radio_Meta_runProcessingThread(struct virtual154RadioObject *const object);

/* -------------------------------------------- API -------------------------------------------- */
void virtual154Radio_init(struct virtual154RadioObject *const object);

void virtual154Radio_setPanId(struct virtual154RadioObject *const object, uint16_t pan_id);

void virtual154Radio_setShortAddress(struct virtual154RadioObject *const object,
				     const uint16_t short_address);

void virtual154Radio_setExtendedAddress(struct virtual154RadioObject *const object,
					const uint8_t *const extended_address);

int8_t virtual154Radio_getTxPowerDbm(const struct virtual154RadioObject *const object);

void virtual154Radio_setTxPower(struct virtual154RadioObject *const object,
				int8_t tx_power_dbm);

int8_t virtual154Radio_getCcaEnergyDetectThreshold(const struct virtual154RadioObject *const object);

void virtual154Radio_setCcaEnergyDetectThreshold(struct virtual154RadioObject *const object,
						 int8_t cca_ed_threshold_dbm);

void virtual154Radio_setLnaGain(struct virtual154RadioObject *const object,
				int8_t lna_gain_dbm);

int8_t virtual154Radio_getLnaGainDbm(const struct virtual154RadioObject *const object);

bool virtual154Radio_getPromiscuousModeState(const struct virtual154RadioObject *const object);

void virtual154Radio_setPromiscuousModeState(struct virtual154RadioObject *const object,
					     bool state);

uint8_t vitrtual154Radio_getChannel(const struct virtual154RadioObject *const object);

void vitrtual154Radio_setChannel(struct virtual154RadioObject *const object, uint8_t channel);

enum virtual154RadioState
virtual154Radio_getState(const struct virtual154RadioObject *const object);

bool virtual154Radio_isEnabled(const struct virtual154RadioObject *const object);

void virtual154Radio_enable(struct virtual154RadioObject *const object);

void virtual154Radio_disable(struct virtual154RadioObject *const object);

bool virtual154Radio_sleep(struct virtual154RadioObject *const object);

bool virtual154Radio_receive(struct virtual154RadioObject *const object);

bool virtual154Radio_sendData(struct virtual154RadioObject *const object,
			      const void *const mpdu, uint8_t mpdu_size);

int8_t virtual154Radio_getRssiDbm(const struct virtual154RadioObject *const object);

void virtual154Radio_registerTxDoneCallback(struct virtual154RadioObject *const object,
					    virtual154RadioTxDoneCallbackFunction *function,
					    void *user_data);

void virtual154Radio_registerRxDoneCallback(struct virtual154RadioObject *const object,
					    virtual154RadioRxDoneCallbackFunction *function,
					    void *user_data);

void virtual154Radio_registerAckDoneCallback(struct virtual154RadioObject *const object,
					     virtual154RadioAckDoneCallbackFunction *function,
					     void *user_data);

