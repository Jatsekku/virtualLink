#include <stdlib.h>
#include <time.h>

#include "dumbFuzzer.h"
#include "unity.h"

#include "virtualLink.h"

#define VIRTUAL_LINK_MTU (128)

#define VIRTUAL_LINK_INTERFACE_IPV4	"127.0.0.1"
#define VIRTUAL_LINK_TX_IPV4_BASE	"127.0.0.1:9000"
#define VIRTUAL_LINK_RX_IPV4		"224.0.0.116:9000"

void setUp(void) {
	time_t t;
	srand((unsigned) time(&t));
}

void tearDown(void) {}

/* Info: Yes, it's not really "by the book" solution, as both send and receive are tested in one
   test, but in this case that will do the job as simple validation */
static void sendAndReceive(struct virtualLinkObject *const object1,
			   struct virtualLinkObject *const object2,
			   const uint8_t *const data, uint32_t data_size) {

	// Send data
	const int16_t send_result = virtualLink_sendDataBlocking(object1, data, data_size);
	TEST_ASSERT(send_result == data_size);

	// Read data
	uint8_t read_data[VIRTUAL_LINK_MTU] = {0};
	const int16_t read_result = virtualLink_receiveDataBlocking(object2,
								    read_data, data_size,
								    VIRTUAL_LINK_WAIT_FOREVER,
								    NULL);
	TEST_ASSERT(read_result == data_size);

	TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_data, data_size);
}


static void sendAndReceive_2receivers(struct virtualLinkObject *const object1,
			   	      struct virtualLinkObject *const object2,
				      struct virtualLinkObject *const object3,
			   	      const uint8_t *const data, uint32_t data_size) {

	// Send data
	const int16_t send_result = virtualLink_sendDataBlocking(object1, data, data_size);
	TEST_ASSERT(send_result == data_size);

	// Read data - 1 receiver
	uint8_t read_data1[VIRTUAL_LINK_MTU] = {0};
	const int16_t read_result1 = virtualLink_receiveDataBlocking(object2,
								     read_data1, data_size,
								     VIRTUAL_LINK_WAIT_FOREVER,
								     NULL);
	TEST_ASSERT(read_result1 == data_size);

	TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_data1, data_size);

	// Read data - 2 receiver
	uint8_t read_data2[VIRTUAL_LINK_MTU] = {0};
	const int16_t read_result2 = virtualLink_receiveDataBlocking(object3,
								     read_data2, data_size,
								     VIRTUAL_LINK_WAIT_FOREVER,
								     NULL);
	TEST_ASSERT(read_result2 == data_size);

	TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_data2, data_size);
}
/* ------------------------------------------- TESTS ------------------------------------------- */

#define TEST_SEND_AND_RECEIVE_ITERATIONS (1000)

void test_sendAndReceive(void) {
	struct virtualLinkConfig virtual_link_config;

	virtualLink_configFromStrings(&virtual_link_config,
				      VIRTUAL_LINK_INTERFACE_IPV4,
				      VIRTUAL_LINK_TX_IPV4_BASE,
				      VIRTUAL_LINK_RX_IPV4);

	struct virtualLinkObject virtual_link1;
	virtualLink_init(&virtual_link1, &virtual_link_config);

	struct virtualLinkObject virtual_link2;
	virtual_link_config.tx_socket_address.port += 1;
	virtualLink_init(&virtual_link2, &virtual_link_config);

	for(int i = 0; i < TEST_SEND_AND_RECEIVE_ITERATIONS; i++) {
		uint8_t sample_data[VIRTUAL_LINK_MTU];
		const uint8_t data_size =
			dumbFuzzer_generateRandomU32InRange(1, sizeof(sample_data));
		dumbFuzzer_genereteRandomData(&sample_data, data_size);

		sendAndReceive(&virtual_link1, &virtual_link2, sample_data, data_size);
	}
}

void test_sendAndReceive_2receivers(void) {
	struct virtualLinkConfig virtual_link_config;

	virtualLink_configFromStrings(&virtual_link_config,
				      VIRTUAL_LINK_INTERFACE_IPV4,
				      VIRTUAL_LINK_TX_IPV4_BASE,
				      VIRTUAL_LINK_RX_IPV4);

	struct virtualLinkObject virtual_link1;
	virtual_link_config.tx_socket_address.port += 1;
	virtualLink_init(&virtual_link1, &virtual_link_config);

	struct virtualLinkObject virtual_link2;
	virtual_link_config.tx_socket_address.port += 1;
	virtualLink_init(&virtual_link2, &virtual_link_config);

	struct virtualLinkObject virtual_link3;
	virtual_link_config.tx_socket_address.port += 1;
	virtualLink_init(&virtual_link3, &virtual_link_config);

	for(int i = 0; i < TEST_SEND_AND_RECEIVE_ITERATIONS; i++) {
		uint8_t sample_data[VIRTUAL_LINK_MTU];
		const uint8_t data_size =
			dumbFuzzer_generateRandomU32InRange(1, sizeof(sample_data));
		dumbFuzzer_genereteRandomData(&sample_data, data_size);

		sendAndReceive_2receivers(&virtual_link1, &virtual_link2, &virtual_link3, sample_data, data_size);
	}
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_sendAndReceive);
	//RUN_TEST(test_sendAndReceive_2receivers);
	return UNITY_END();
}
