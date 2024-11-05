#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "virtual154Radio.h"
#include "lmac154Frame.h"

#define VIRTUAL_LINK_INTERFACE_IPV4	"127.0.0.1"
#define VIRTUAL_LINK_TX_IPV4_BASE	"127.0.0.1:9001"
#define VIRTUAL_LINK_RX_IPV4		"224.0.0.116:9000"

#define SOURCE_PANID		(0x2137)
#define SOURCE_SHORT_ADDR	(0x0420)

#define RADIO_CHANNEL		(20)

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
			0x09, 0x0A, 0x0B, 0x0C,
			0x0D, 0x0E, 0x0F, 0x01
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

	/* Set 16-bit address specific to this particular node */
	virtual154Radio_setShortAddress(&radio, SOURCE_SHORT_ADDR);

	// Set channel
	vitrtual154Radio_setChannel(&radio, RADIO_CHANNEL);

        // Receive data
        virtual154Radio_receive(&radio);
}