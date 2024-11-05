#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "virtual154Radio.h"
#include "lmac154Frame.h"

#define VIRTUAL_LINK_INTERFACE_IPV4	"127.0.0.1"
#define VIRTUAL_LINK_TX_IPV4_BASE	"127.0.0.1:9000"
#define VIRTUAL_LINK_RX_IPV4		"224.0.0.116:9000"

#define SOURCE_PANID		(0x2137)
#define SOURCE_SHORT_ADDR	(0x1234)

#define DESTINATION_PANID	(0x2137)
#define DESTINATION_SHORT_ADDR	(0x0420)

#define RADIO_CHANNEL		(20)

void txDoneCallback(enum virtual154RadioTxStatusCode status,
		    void *user_data) {
	printf("TX done: %d\n", status);
}

void ackDoneCallback(enum virtual154RadioAckStatusCode status, 
		     const void *const mpdu, uint8_t mpdu_size,
		     void *user_data) {
	printf("ACK done: %d\n", status);
}

int main(void) {

	// This part is mostly emulation-specific

        /* Create virtualLink object
         * It's needed for radio backend as "RF medium mockup"
         * It's part of emulation - not important from perspective of SW runnig on real HW
         */ 
        struct virtualLinkObject virtual_link;
	struct virtualLinkConfig virtual_link_config;
	virtualLink_configFromStrings(&virtual_link_config,
				      VIRTUAL_LINK_INTERFACE_IPV4,
				      VIRTUAL_LINK_TX_IPV4_BASE,
				      VIRTUAL_LINK_RX_IPV4);
	virtualLink_init(&virtual_link, &virtual_link_config);

	/* Create virtual154Radio object
	 * Information provided here in reality are determined by hardware constrains
	 * or are not even present in any form.
	 * It's part of emulation - not important from perspective of SW runnig on real HW
	 */
	struct virtual154RadioObject radio;
	struct virtual154RadioMetaConfig radio_meta_config = {
		.rx_sensitivity_dbm = -104,	// Radio RX sensitivity - determined by RF front-end
		.eui64_address = {		// Something like MAC address
			0x01, 0x02, 0x03, 0x04,
			0x05, 0x06, 0x07, 0x08
		},
		.virtual_link = &virtual_link, 	// "RF medium mockup"
		.bus_speed = 0, 		// Unused anyway,
		.skip_shr_and_phr = true 	// Emulation-related
	};
	virtual154Radio_Meta_init(&radio, &radio_meta_config);

	// From here the code would be quite similar to this running on real HW

	// Initialize the radio
	virtual154Radio_init(&radio);

	// Set 16-bit Personal Area Network ID
	virtual154Radio_setPanId(&radio, SOURCE_PANID);

	/* Set 16-bit address specific to this particular node
	 * It's rather important for RX than TX, but I placed it here for compleetness
	 */
	virtual154Radio_setShortAddress(&radio, SOURCE_SHORT_ADDR);

	// Set TX power
	virtual154Radio_setTxPower(&radio, 12);

	// Set channel
	vitrtual154Radio_setChannel(&radio, RADIO_CHANNEL);

	// Register TX and ACK callbacks
	virtual154Radio_registerTxDoneCallback(&radio, txDoneCallback, NULL);
	virtual154Radio_registerAckDoneCallback(&radio, ackDoneCallback, NULL);

	// Enable radio hardware
	virtual154Radio_enable(&radio);

	// Allocate buffer for TX frame
	uint8_t tx_frame_buffer[128] = {0};

	// It will be data frame
	lmac154Frame_setType(&tx_frame_buffer, LMAC154_FRAME_TYPE_DATA);

	// Turn off security - no encoding
	lmac154Frame_setSecurityField(&tx_frame_buffer, false);

	// No pending frame
	lmac154Frame_setPendingFrameNotification(&tx_frame_buffer, false);

	// Request ACK
	lmac154Frame_setAckRequest(&tx_frame_buffer, true);

	// Intra PAD
	lmac154Frame_setIntraPan(&tx_frame_buffer, true);

	// Source address - 16 bit addresing mode, 0x1234
	lmac154Frame_setSourceAddress(&tx_frame_buffer,
				      LMAC154_ADDRESSING_MODE_16BIT, SOURCE_SHORT_ADDR);

	// Destination address - 16 bit addresing mode, 0x0420
	lmac154Frame_setDestinationAddress(&tx_frame_buffer,
					   LMAC154_ADDRESSING_MODE_16BIT, DESTINATION_SHORT_ADDR);

	// Destination PANID
	lmac154Frame_setDestinationPanId(&tx_frame_buffer, DESTINATION_PANID);

	const uint8_t payload[] = "randompayload";
	struct lmac154FrameResult result = lmac154Frame_appendPayloadAfterHeader(&tx_frame_buffer,
										 payload, 
										 sizeof(payload));

	if (result.tag != LMAC154_FRAME_TAG_OK_U8) {
		printf("Failed to construct TX frame\n");
	}

	virtual154Radio_receive(&radio);

	while(1) {
		virtual154Radio_sendData(&radio, &tx_frame_buffer, result.value.ok_u8);
		sleep(1);
		// That is normally done by radio hardware
		virtual154Radio_Meta_processingLoop(&radio);
	}
}