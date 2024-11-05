#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <stdio.h>

#include <unistd.h>
#include <inttypes.h>

#include "logger/logger.h"
#include "lmac154Frame.h"
#include "virtualLink.h"
#include "systemTime.h"

#include "virtual154Radio.h"

LOGGER_REGISTER_MODULE("virtual154Radio", LOG_LEVEL_DBG);

#define DEFAULT_PAN_ID (0x4321)
#define DEFAULT_SHORT_ADDRESS (0x0000)
#define DEFAULT_EXTENDED_ADDRESS {0xAC, 0xDE, 0x48, 0x00, 0x00, 0x00, 0x00, 0x02}
#define DEFAULT_CCA_ED_THRESHOLD_DBM (-71)
#define DEFAULT_LNA_GAIN_DBM (0)
#define DEFAULT_PROMISCUOUS_MODE_STATE (false)
#define DEFAULT_CHANNEL (11)

#define MAX_PHY_PACKET_SIZE (256)
#define EUI64_ADDRESS_SIZE (8)

struct __attribute__ ((__packed__)) virtua154RadioShr {
	uint32_t preamble;
	uint8_t sfd;
};

struct __attribute__ ((__packed__)) virtual154RadioPhr {
	uint8_t frame_length : 7;
	uint8_t reserved_0   : 1;
};

struct __attribute__ ((__packed__)) virtual154RadioPsdu {
	uint8_t mpdu[MAX_PHY_PACKET_SIZE];
};

struct __attribute__ ((__packed__)) virtual154RadioPpdu {
	struct virtua154RadioShr shr;
	struct virtual154RadioPhr phr;
	struct virtual154RadioPsdu psdu;
};

static inline void fillShr(struct virtua154RadioShr *const shr) {
	assert((NULL != shr)
	       && "shr cannot be NULL");

	const uint32_t preamble = 0b00000000000000000000000000000000; // 32 x b0
	const uint8_t sfd = 0b10100111;

	shr->preamble = preamble;
	shr->sfd = sfd;
}

static inline void fillPhr(struct virtual154RadioPhr *const phr, uint8_t frame_length) {
	assert((NULL != phr)
	       && "phr cannot be NULL");
	assert((4 < frame_length)
	       && "frame_length <0;4> reserved value");
	assert((6 != frame_length && 7 != frame_length)
	       && "frame_length <6;7> reserved value");

	phr->reserved_0 = 0;
	phr->frame_length = frame_length;
}

static inline void fillPsdu(struct virtual154RadioPsdu *const psdu,
			    const void *const payload, uint8_t payload_size) {
	assert((NULL != psdu)
	       && "psdu cannot be NULL");
	assert((NULL != payload)
	       && "payload cannot be NULL");
	assert((MAX_PHY_PACKET_SIZE >= payload_size)
	       && "payload_size cannot be larger than MAX_PHY_PACKET_SIZE");

	memcpy(psdu->mpdu, payload, payload_size);
}

struct __attribute__ ((__packed__)) virtual154RadioMetaRfData {
	uint8_t channel;
};

struct __attribute__ ((__packed__)) virtual154RadioRfData {
	struct virtual154RadioMetaRfData meta_data;
	struct virtual154RadioPpdu ppdu;
};

struct __attribute__ ((__packed__)) virtual154RadioRfDataNoShrNoPhr {
	struct virtual154RadioMetaRfData meta_data;
	struct virtual154RadioPsdu psdu;
};

static inline uint16_t assemblyRfFrame(bool skip_shr_and_phr,
				       const uint8_t *const mpdu, uint8_t mpdu_size,
				       uint8_t channel,
				       void *const rf_frame_buffer) {
	assert((NULL != mpdu)
	       && "mpdu cannot be NULL");
	assert((MAX_PHY_PACKET_SIZE >= mpdu_size)
	       && "mpdu_size cannot be larger than MAX_PHY_PACKET_SIZE");
	assert((11 <= channel) && (26 >= channel)
	       && "chanel has to be in range <11;26>");
	assert((NULL != rf_frame_buffer)
	       && "rf_frame_buffer cannot be NULL");

	uint16_t rf_frame_size = 0;

	// Assembly frame without shr and phr section
	if (skip_shr_and_phr) {
		struct virtual154RadioRfDataNoShrNoPhr *const rf_frame = rf_frame_buffer;
		rf_frame->meta_data.channel = channel;
		fillPsdu(&rf_frame->psdu, mpdu, mpdu_size);
		rf_frame_size = sizeof(struct virtual154RadioMetaRfData) + mpdu_size;
	} else {
		struct virtual154RadioRfData *const rf_frame = rf_frame_buffer;
		rf_frame->meta_data.channel = channel;
		fillShr(&rf_frame->ppdu.shr);
		fillPhr(&rf_frame->ppdu.phr, mpdu_size);
		fillPsdu(&rf_frame->ppdu.psdu, mpdu, mpdu_size);
		rf_frame_size = sizeof(struct virtual154RadioMetaRfData)
			      + sizeof(struct virtua154RadioShr)
			      + sizeof(struct virtual154RadioPhr)
			      + rf_frame->ppdu.phr.frame_length;
	}

	return rf_frame_size;
}

static void sendRfFrame(const struct virtual154RadioObject *const object,
			const uint8_t *const mpdu, uint8_t mpdu_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != mpdu)
	       && "mpdu cannot be NULL");
	assert((MAX_PHY_PACKET_SIZE >= mpdu_size)
	       && "mpdu_size cannot be larger than MAX_PHY_PACKET_SIZE");

	//TODO: make it external?
	uint8_t rf_frame[sizeof(struct virtual154RadioRfData)];
	const uint16_t rf_frame_size = assemblyRfFrame(object->_meta_config.skip_shr_and_phr,
						       mpdu, mpdu_size, object->_channel,
						       &rf_frame);

	const int16_t sent_data_size =
		virtualLink_sendDataBlocking(object->_meta_config.virtual_link,
					     &rf_frame, rf_frame_size);

	assert(rf_frame_size == sent_data_size
	       && "failed to send all data");
}

static inline bool channelFilter(const struct virtual154RadioObject *const object,
				 uint8_t frame_channel) {
	assert((NULL != object)
	       && "object cannot be NULL");

	const uint8_t radio_channel = object->_channel;
	const bool result = (radio_channel == frame_channel);

	LOG_DBG("%s(radio_channel=%d, frame_channel=%d) -> %d",
		(const char*)__PRETTY_FUNCTION__,
		(int)radio_channel,
		(int)frame_channel,
		(int)result);

	return result;
}

static inline bool panIdFilter(const struct virtual154RadioObject *const object,
			       const uint8_t *const lmac_frame) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != lmac_frame)
	       && "lmac_frame cannot be NULL");

	const uint16_t radio_pan_id = object->_pan_id;
	const struct lmac154FrameResult frame_pan_id =
		lmac154Frame_getDestinationPanId(lmac_frame);

	if (LMAC154_FRAME_TAG_OK_U16 != frame_pan_id.tag) {
		return false;
	}

	const uint16_t frame_pan_id_valid = frame_pan_id.value.ok_u16;

	const bool result = (radio_pan_id == frame_pan_id_valid)
			    || (LMAC154_PAN_ID_BROADCAST == frame_pan_id_valid);

	LOG_DBG("%s(radio_panid=%d, frame_panid=%d) -> %d",
		(const char*)__PRETTY_FUNCTION__,
		(int)radio_pan_id,
		(int)frame_pan_id_valid,
		(int)result);

	return result;
}

static inline bool addressFilter(const struct virtual154RadioObject *const object,
				 const uint8_t *const lmac_frame) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != lmac_frame)
	       && "lmac_frame cannot be NULL");

	const struct lmac154FrameResult frame_dest_address =
		lmac154Frame_getDestinationAddress(lmac_frame);

	bool result;
	uint64_t radio_address;
	uint64_t frame_dest_address_valid;

	switch (frame_dest_address.tag) {
		case LMAC154_FRAME_TAG_OK_U16:
			radio_address = object->_short_address;
			frame_dest_address_valid = frame_dest_address.value.ok_u16;
			result = (radio_address == frame_dest_address_valid)
				 || (LMAC154_SHORT_ADDRESS_BROADCAST == frame_dest_address_valid);
		break;

		case LMAC154_FRAME_TAG_OK_U64:
			memcpy(&radio_address,
			       object->_extended_address,
			       VIRTUAL_154_RADIO_EXTENDED_ADDRESS_SIZE);
			frame_dest_address_valid = frame_dest_address.value.ok_u64;
			result = (radio_address == frame_dest_address_valid);
		break;

		default:
			LOG_ERR("%s() -> 0 No addresing field!", (const char*)__PRETTY_FUNCTION__);
			return false;
		break;
	}

	LOG_DBG("%s(extended=%d,radio_address=%d, frame_dest_address=%d) -> %d",
		(const char*)__PRETTY_FUNCTION__,
		(int)(frame_dest_address.tag == LMAC154_FRAME_TAG_OK_U64),
		(int)radio_address,
		(int)frame_dest_address_valid,
		(int)result);

	return result;
}

static inline bool lmacFrameFilter(const struct virtual154RadioObject *const object,
				   const uint8_t *const lmac_frame) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != lmac_frame)
	       && "lmac_frame cannot be NULL");

	// Check if received frame was sent in the same PAN (or broadcast)
	if (!panIdFilter(object, lmac_frame)) {
		return false;
	}

	// Check if received frame was addressed to this particular radio device (or broadcast)
	if (!addressFilter(object, lmac_frame)) {
		return false;
	}

	return true;
}

static void processAckFrame(struct virtual154RadioObject *const object,
			    const void *const psdu, uint8_t psdu_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != psdu)
	       && "psdu cannot be NULL");

	const uint8_t received_sequence_number = lmac154Frame_getSequenceNumber(psdu);
	const uint8_t expected_sequence_number = object->_ack_data.sequence_number;

	if (expected_sequence_number == received_sequence_number) {
		// TODO add callback check
		object->_ack_done_callback.function(VIRTUAL_154_RADIO_ACK_STATUS_CODE_OK,
						    psdu, psdu_size,
						    object->_ack_done_callback.user_data);
		object->_ack_data.is_expected = false;
	}
}

static void processAckTimeout(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if (object->_ack_data.is_expected) {
		const uint64_t now = systemTime_getFreezableEpochUs();
		const uint64_t expire_timestamp =
			object->_ack_data.start_timestamp_us + object->_ack_data.timeout; 
		if (now > expire_timestamp) {
			LOG_WRN("ACK timeout");
			object->_ack_done_callback.function(VIRTUAL_154_RADIO_ACK_STATUS_CODE_TIMEOUT,
						    	    NULL, 0,
						    	    object->_ack_done_callback.user_data);
			object->_ack_data.is_expected = false;		
		}
	}
}

static void respondWithAck(const struct virtual154RadioObject *const object,
			   bool frame_pending, uint8_t sequence_number) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	uint8_t ack_frame[5] = {0};
	lmac154Frame_setType(ack_frame, LMAC154_FRAME_TYPE_ACK);
	lmac154Frame_setPendingFrameNotification(ack_frame, frame_pending);
	lmac154Frame_setSequenceNumber(ack_frame, sequence_number);
	//TODO: calc CRC

	sendRfFrame(object, ack_frame, sizeof(ack_frame));
}

static void processDataFrame(const struct virtual154RadioObject *const object,
			     const void *const psdu, uint8_t psdu_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != psdu)
	       && "psdu cannot be NULL");

	// Apply filtering if promiscuous mode is not enabled 
	if (!object->_promiscuous_mode_state) {
		const uint8_t *const lmac_frame = psdu;
		if (!lmacFrameFilter(object, lmac_frame)) {
			return;
		}
	}

	// Respond with ACK if requested
	const bool is_ack_response_expected = lmac154Frame_getAckRequest(psdu);
	const uint8_t sequence_number = lmac154Frame_getSequenceNumber(psdu);

	if (is_ack_response_expected) {
		//TODO: Pending frame???
		respondWithAck(object, 0, sequence_number);
	}

	if (NULL != object->_rx_done_callback.function) {
		object->_rx_done_callback.function(VIRTUAL_154_RADIO_RX_STATUS_CODE_OK,
						   psdu, psdu_size,
						   object->_rx_done_callback.user_data);
	}	
}

static void processRfFrame(struct virtual154RadioObject *const object,
			   const void *const rf_frame, uint16_t rf_frame_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != rf_frame)
	       && "rf_frame cannot be NULL");

	if (0 == rf_frame_size) {
		return;
	}

	const uint8_t *mpdu = NULL;
	uint8_t mpdu_size = 0;
	uint8_t frame_channel = 0;

	if (object->_meta_config.skip_shr_and_phr) {
		const struct virtual154RadioRfDataNoShrNoPhr *const _rf_frame = rf_frame;
		mpdu = _rf_frame->psdu.mpdu;
		mpdu_size = rf_frame_size - sizeof(struct virtual154RadioMetaRfData);
		frame_channel = _rf_frame->meta_data.channel;
	} else {
		const struct virtual154RadioRfData *const _rf_frame = rf_frame;
		mpdu = _rf_frame->ppdu.psdu.mpdu;
		mpdu_size = _rf_frame->ppdu.phr.frame_length;
		frame_channel = _rf_frame->meta_data.channel;
	}

	// Check if received frame was sent on same channel that radio is listening to
	if (!channelFilter(object, frame_channel)) {
		return;
	}

	const enum lmac154FrameType frame_type = lmac154Frame_getType(mpdu);  

	switch (frame_type) {
		case LMAC154_FRAME_TYPE_BEACON:
			LOG_DBG("Beacon frame - unsupported\n");
		break;
	
		case LMAC154_FRAME_TYPE_DATA:
			LOG_DBG("Data frame\n");
			processDataFrame(object, mpdu, mpdu_size);
		break;

		case LMAC154_FRAME_TYPE_ACK:
			if (object->_ack_data.is_expected) {
				LOG_DBG("Expected ACK frame\n");
				processAckFrame(object, mpdu, mpdu_size);
			} else {
				LOG_WRN("Unexpected ACK frame\n")
				return;
			}
			break;

		case LMAC154_FRAME_TYPE_CMD:
			LOG_DBG("Command frame - unsupported\n");
		break;

		default:
			LOG_WRN("Unknown frame\n");
		break;
	}
}

static void receiveRfFrame(struct virtual154RadioObject *const object, int timeout_ms) {
	assert((NULL != object)
	       && "object cannot be NULL");

	struct virtual154RadioRfData rf_frame = {0};
	struct virtualLinkSocketAddress originator_address;

	//TODO: Check if whole packet has been received
	const int16_t rf_frame_size =
		virtualLink_receiveDataBlocking(object->_meta_config.virtual_link,
						&rf_frame, sizeof(rf_frame),
						timeout_ms,
						&originator_address);

	if (rf_frame_size > 0) {
		processRfFrame(object, &rf_frame, rf_frame_size);
	}
}

static void copyMetaConfig(struct virtual154RadioObject *const object,
			   const struct virtual154RadioMetaConfig *const meta_config) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != meta_config)
	       && "meta_config cannot be NULL");

	object->_meta_config = *meta_config;
}

static void *processingThread(void *arg) {
	struct virtual154RadioObject *const object = arg;
	while(1) {
		processAckTimeout(object);
		switch (object->_state) {
			case VIRTUAL_154_RADIO_STATE_RX:
				receiveRfFrame(object, VIRTUAL_LINK_WAIT_FOREVER);
			break;
		}
	}
}

static inline void callTxDoneCallback(const struct virtual154RadioObject *const object,
				      enum virtual154RadioTxStatusCode status) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if (NULL != object->_tx_done_callback.function) {
		object->_tx_done_callback.function(status, object->_tx_done_callback.user_data);
	}
}
/* ----------------------------------------- Meta API ------------------------------------------ */
void virtual154Radio_Meta_init(struct virtual154RadioObject *const object,
			       const struct virtual154RadioMetaConfig *const meta_config) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != meta_config)
	       && "meta_config cannot be NULL");

	copyMetaConfig(object, meta_config);		
}

void virtual154Radio_Meta_setRxSensitivity(struct virtual154RadioObject *const object,
					   int8_t rx_sensitivity_dbm) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_meta_config.rx_sensitivity_dbm = rx_sensitivity_dbm;
}

int8_t virtual154Radio_Meta_getRxSensitivity(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	return object->_meta_config.rx_sensitivity_dbm;
}

void virtual154Radio_Meta_setEui64Address(struct virtual154RadioObject *const object,
					  const uint8_t *const eui64_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != eui64_address)
	       && "eui64_address cannot be NULL");

	memcpy(object->_meta_config.eui64_address, eui64_address, EUI64_ADDRESS_SIZE);
}

void virtual154Radio_Meta_getEui64Address(const struct virtual154RadioObject *const object,
					  uint8_t *const eui64_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != eui64_address)
	       && "eui64_address cannot be NULL");

	memcpy(eui64_address, object->_meta_config.eui64_address, EUI64_ADDRESS_SIZE);
}

void virtual154Radio_Meta_setRssi(struct virtual154RadioObject *const object, int8_t rssi_dbm) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	object->_meta_data.rssi_dbm = rssi_dbm;
}

void virtual154Radio_Meta_processingLoop(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	processAckTimeout(object);
	switch (object->_state) {
		case VIRTUAL_154_RADIO_STATE_RX:
			receiveRfFrame(object, VIRTUAL_LINK_DONT_WAIT);
		break;
	}
}

void virtual154Radio_Meta_runProcessingThread(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	pthread_t thread;
	pthread_create(&thread, NULL, processingThread, (void*)object);
}

/* -------------------------------------------- API -------------------------------------------- */
void virtual154Radio_setPanId(struct virtual154RadioObject *const object, uint16_t pan_id) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_pan_id = pan_id;
}

void virtual154Radio_setShortAddress(struct virtual154RadioObject *const object,
				     const uint16_t short_address) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_short_address = short_address;
}

void virtual154Radio_setExtendedAddress(struct virtual154RadioObject *const object,
					const uint8_t *const extended_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != extended_address)
	       && "extended_address cannot be NULL");
	
	memcpy(object->_extended_address, extended_address, VIRTUAL_154_RADIO_EXTENDED_ADDRESS_SIZE);
}

int8_t virtual154Radio_getTxPowerDbm(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	return object->_tx_power_dbm;
}

void virtual154Radio_setTxPower(struct virtual154RadioObject *const object,
				int8_t tx_power_dbm) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_tx_power_dbm = tx_power_dbm;
}

int8_t virtual154Radio_getCcaEnergyDetectThreshold(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	return object->_cca_ed_threshold_dbm;
}

void virtual154Radio_setCcaEnergyDetectThreshold(struct virtual154RadioObject *const object,
						 int8_t cca_ed_threshold_dbm) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	object->_cca_ed_threshold_dbm = cca_ed_threshold_dbm;
}

int8_t virtual154Radio_getLnaGainDbm(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	return object->_lna_gain_dbm;
}

void virtual154Radio_setLnaGain(struct virtual154RadioObject *const object,
				int8_t lna_gain_dbm) {
	assert((NULL != object)
	       && "object cannot be NULL");
	
	object->_lna_gain_dbm = lna_gain_dbm;
}

bool virtual154Radio_getPromiscuousModeState(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	return object->_promiscuous_mode_state;
}

void virtual154Radio_setPromiscuousModeState(struct virtual154RadioObject *const object,
					     bool state) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_promiscuous_mode_state = state;
}

uint8_t vitrtual154Radio_getChannel(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	return object->_channel;
}

void vitrtual154Radio_setChannel(struct virtual154RadioObject *const object, uint8_t channel) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((11 <= channel) && (26 >= channel)
	       && "chanel has to be in range <11;26>");

	object->_channel = channel;
}

enum virtual154RadioState
virtual154Radio_getState(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	return object->_state;
}

bool virtual154Radio_isEnabled(const struct virtual154RadioObject *const object) {
	return (object->_state != VIRTUAL_154_RADIO_STATE_DISABLED);
}

void virtual154Radio_enable(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if(!virtual154Radio_isEnabled(object)) {
		object->_state = VIRTUAL_154_RADIO_STATE_SLEEP;
	}
}

void virtual154Radio_disable(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if(!virtual154Radio_isEnabled) {
		object->_state = VIRTUAL_154_RADIO_STATE_SLEEP;
	}
}

bool virtual154Radio_sleep(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if (object->_state == VIRTUAL_154_RADIO_STATE_SLEEP ||
	    object->_state == VIRTUAL_154_RADIO_STATE_RX) {
		object->_state = VIRTUAL_154_RADIO_STATE_SLEEP;
		return true;
	}

	// Invalid state
	return false;
}

bool virtual154Radio_receive(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	if (object->_state != VIRTUAL_154_RADIO_STATE_DISABLED) {
		object->_state = VIRTUAL_154_RADIO_STATE_RX;
		return true;
	}

	// Invalid state
	return false;
}

bool virtual154Radio_sendData(struct virtual154RadioObject *const object,
			      const void *const mpdu, uint8_t mpdu_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != mpdu)
	       && "object cannot be NULL");
	assert((0 != mpdu_size)
	       && "mpdu_size cannot be zero");

	LOG_DBG("%s(mpdu_size=%" PRIu8 ")",
		(const char*)__PRETTY_FUNCTION__, (unsigned int)mpdu_size);

	if (object->_state != VIRTUAL_154_RADIO_STATE_RX) {
		LOG_WRN("%s -> %s",
			(const char*)__PRETTY_FUNCTION__,
			(const char*)"false");
		
		return false;
	}

	// Check if ACK for this transmission has been requested
	bool is_ack_expected = lmac154Frame_getAckRequest(mpdu);
	if (is_ack_expected) {
		object->_ack_data.is_expected = true;
		object->_ack_data.sequence_number = lmac154Frame_getSequenceNumber(mpdu);
		object->_ack_data.start_timestamp_us = systemTime_getFreezableEpochUs(); 
	}

	LOG_DBG("_ack_data { .is_expected=%d" PRIu8
		", .sequence_number=%d"
		// ", .start_timestamp_us=" PRIu64,
		"}",
		(int)object->_ack_data.is_expected,
		(int)object->_ack_data.sequence_number)
		// object->_ack_data.start_timestamp_us);
	
	sendRfFrame(object, mpdu, mpdu_size);

	// NO Abortion or CSMA handling for now
	callTxDoneCallback(object, VIRTUAL_154_RADIO_TX_STATUS_CODE_OK);

	object->_state = VIRTUAL_154_RADIO_STATE_RX;
	return true;
}

int8_t virtual154Radio_getRssiDbm(const struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	return object->_meta_data.rssi_dbm;
}

void virtual154Radio_registerTxDoneCallback(struct virtual154RadioObject *const object,
					    virtual154RadioTxDoneCallbackFunction *function,
					    void *user_data) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_tx_done_callback.function = function;
	object->_tx_done_callback.user_data = user_data;	
}

void virtual154Radio_registerRxDoneCallback(struct virtual154RadioObject *const object,
					    virtual154RadioRxDoneCallbackFunction *function,
					    void *user_data) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_rx_done_callback.function = function;
	object->_rx_done_callback.user_data = user_data;	
}

void virtual154Radio_registerAckDoneCallback(struct virtual154RadioObject *const object,
					     virtual154RadioAckDoneCallbackFunction *function,
					     void *user_data) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_ack_done_callback.function = function;
	object->_ack_done_callback.user_data = user_data;	
}

void virtual154Radio_init(struct virtual154RadioObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");

	object->_pan_id = DEFAULT_PAN_ID;
	object->_short_address = DEFAULT_SHORT_ADDRESS;
	const uint8_t default_extended_address[] = DEFAULT_EXTENDED_ADDRESS;
	// object->_extended_address = default_extended_address;
	memcpy(object->_extended_address,
	       default_extended_address,
	       sizeof(object->_extended_address));
	object->_cca_ed_threshold_dbm = DEFAULT_CCA_ED_THRESHOLD_DBM;
	object->_lna_gain_dbm = DEFAULT_LNA_GAIN_DBM;
	object->_promiscuous_mode_state = DEFAULT_PROMISCUOUS_MODE_STATE;
	object->_channel = DEFAULT_CHANNEL;
	object->_state = VIRTUAL_154_RADIO_STATE_DISABLED;
	object->_ack_data.is_expected = false;

	object->_tx_done_callback.function = NULL;
	object->_tx_done_callback.user_data = NULL;

	object->_rx_done_callback.function = NULL;
	object->_rx_done_callback.user_data = NULL;

	object->_ack_data.timeout = 100;

	object->_is_initialized = true;
}
