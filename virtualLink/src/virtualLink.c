#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "logger/logger.h"
#include "systemTime.h"
#include "virtualLink.h"

LOGGER_REGISTER_MODULE("virtualLink", LOG_LEVEL_NONE);

/*
static void printSocketAddress(const struct sockaddr_in *const socket_address) {
	char ipv4_string[sizeof("255.255.255.255")];
	char port_string[sizeof("65535")];

	getnameinfo((struct sockaddr *)socket_address, sizeof(struct sockaddr),
		    ipv4_string, sizeof(ipv4_string),
		    port_string, sizeof(port_string),
		    NI_NUMERICHOST | NI_NUMERICSERV);

	printf("%s:%s\n", ipv4_string, port_string);
}

static inline void printIpv4Address(const struct in_addr ipv4_address) {
	char *ipv4_address_string = inet_ntoa(ipv4_address);
	if (NULL != ipv4_address_string) {
		printf("%s\n", ipv4_address_string);
	}
}
*/

static bool socketAddressFromString(const char *const socket_address_string,
				    uint32_t *const ipv4_address,
				    uint16_t *const port) {
	assert((NULL != socket_address_string)
	       && "socket_address_string cannot be NULL");
	assert((NULL != ipv4_address)
	       && "ipv4_address cannot be NULL");
	assert((NULL != port)
	       && "port cannot be NULL");

	char ip_v4_address_string[sizeof("255.255.255.255")] = {0};
	bool was_colon_detected = false;
	uint8_t colon_position;

	// Get IPv4 address
	for(int i = 0; i < sizeof(ip_v4_address_string); i++) {
		char c = socket_address_string[i];

		if (c == ':') {
			was_colon_detected = true;
			colon_position = i;
			ip_v4_address_string[i] = '\0';
			break;
		}

		ip_v4_address_string[i] = c;
	}

	// Convert IPv4 addres string into in_addr_t
	const uint32_t ipv4_address_number = (uint32_t)inet_addr(ip_v4_address_string);
	if (INADDR_NONE == ipv4_address_number) {
		// Failed to convert ip_v4_address_string into in_addr_t
		return false;
	}
	// Succesfully converted ipv4 address
	*ipv4_address = ntohl(ipv4_address_number);

	char port_string[sizeof("65535")] = {0};
	const uint8_t j = colon_position + 1;

	// Get port number
	for(int i = 0; i < sizeof(port_string); i++) {
		char c = socket_address_string[j + i];

		if (c == '\0') {
			port_string[i] = '\0';
			break;
		}

		port_string[i] = c;
	}

	char *port_string_last_char;
	uint16_t port_number = (uint16_t)strtol(port_string, &port_string_last_char, 10);
	if (*port_string_last_char != '\0') {
		// Failed to convert port_string into number
		return false;
	}
	// Succesfully converted port
	*port = port_number;

	return true;
}

static inline void copyConfig(struct virtualLinkObject *const object,
                	      const struct virtualLinkConfig *const config) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != config)
	       && "config cannot be NULL");

	object->_config = *config;
}

static inline int initTxSocket(const struct sockaddr_in *const tx_socket_address) {
	assert((NULL != tx_socket_address)
	       && "socket_address cannot be NULL");

	// Create new socket
	const int new_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	assert((-1 != new_socket_fd)
	       && "Failed to create socket");

	// Permit sending multicast messages on socket
	int ret = setsockopt(new_socket_fd,
			     IPPROTO_IP,
			     IP_MULTICAST_IF,
			     &tx_socket_address->sin_addr, sizeof(struct in_addr));
	assert((-1 != ret)
	       && "Failed to set IP_MULTICAST_IF option");

	//Enable multicast looping - transmited message will be delivered also to sending host
	const int one = 1;
	ret = setsockopt(new_socket_fd,
			 IPPROTO_IP,
			 IP_MULTICAST_LOOP,
			 &one, sizeof(one));
	assert((-1 != ret)
	       && "Failed to set IP_MULTICAST_LOOP option");

	// Bind socket with address
    	ret = bind(new_socket_fd,
		   (struct sockaddr *)tx_socket_address, sizeof(struct sockaddr_in));
	assert((-1 != ret)
	       && "Failed to bind socket");

	// Succesfully initialized tx socket
	return new_socket_fd;
}

static inline int initRxSocket(const struct sockaddr_in *const rx_socket_address) {
	assert((NULL != rx_socket_address)
	       && "socket_address cannot be NULL");

	// Create new socket
	const int new_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	assert((-1 != new_socket_fd)
	       && "Failed to create socket");

	// Enable address reusing
	const int one = 1;
	int ret = setsockopt(new_socket_fd,
			     SOL_SOCKET,
			     SO_REUSEADDR,
			     &one, sizeof(one));
	assert((-1 != ret)
	       && "Failed to set SO_REUSEADDR option");

	// Enable port reusing
	ret = setsockopt(new_socket_fd,
			 SOL_SOCKET,
			 SO_REUSEPORT,
			 &one, sizeof(one));
	assert((-1 != ret)
	       && "Failed to set SO_REUSEPORT option");

	// Bind socket with address
    	ret = bind(new_socket_fd,
		   (struct sockaddr *)rx_socket_address, sizeof(struct sockaddr_in));
	assert((-1 != ret)
	       && "Failed to bind socket");

	// Succesfully initialized rx socket
	return new_socket_fd;
}

static inline int createEpoll(void) {
	const int epoll_fd = epoll_create1(0);
	assert((-1 != epoll_fd)
		&& "Failed to create epoll file desciptor");
	return epoll_fd;
}

static inline void addObservableFileDescriptor(int epoll_fd,
					       int observable_fd, int events) {
	struct epoll_event event = {
		.events = events,
		.data.fd = observable_fd,
	};

	const int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, observable_fd, &event);
	assert((0 == err)
	       && "Failed to add file descriptor as epoll event");
}

static inline void attachSocketToMulticastGroup(int socket_fd,
						uint32_t ipv4_interface_address,
						uint32_t ipv4_multicast_group_address) {
	const struct ip_mreq mreq = {
		.imr_interface.s_addr = ipv4_interface_address,
		.imr_multiaddr.s_addr = ipv4_multicast_group_address,
	};

	const int ret = setsockopt(socket_fd,
				   IPPROTO_IP,
				   IP_ADD_MEMBERSHIP,
				   &mreq, sizeof(mreq));
	assert((-1 != ret)
	       && "Failed to set IP_ADD_MEMBERSHIP option");
}

static inline bool compareSocketAddress(const struct virtualLinkSocketAddress *const a,
					const struct virtualLinkSocketAddress *const b) {
	assert((NULL != a)
	       && "a cannot be NULL");
	assert((NULL != b)
	       && "b cannot be NULL");

	LOG_DBG("%s(a=%" PRIu32 ",%" PRIu16 ",b=%" PRIu32 ".%" PRIu16 ")",
		(const char*)__PRETTY_FUNCTION__,
		a->ipv4_address, a->port, b->ipv4_address, b->port);

	return ((a->ipv4_address == b->ipv4_address) && (a->port == b->port));
} 

static inline bool isRxInterruptEnabled(const struct virtualLinkObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	return object->_is_rx_interrupt_enabled;
}

static inline void
callRxDoneCallback(const struct virtualLinkObject *const object,
		   const void *const rx_data, size_t rx_data_size,
		   const struct virtualLinkSocketAddress *const originator_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	if (NULL == object->_rx_done_callback.function) {
		object->_rx_done_callback.function(rx_data, rx_data_size,
						   originator_address,
						   object->_rx_done_callback.user_data);
	}
}

static inline bool isRxDataAwaiting(const struct virtualLinkObject *const object,
				    int timeout_ms) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	struct epoll_event event;
	const int ret = epoll_wait(object->_epoll_descriptor, &event, 1, timeout_ms);
	assert((0 <= ret)
	       && "Failed to get count of epoll events");
	return 1 <= ret;
}

static inline size_t receiveData(const struct virtualLinkObject *const object,
				 void *const rx_buffer, size_t rx_bytes_read_size, 
				 struct virtualLinkSocketAddress *const originator_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");
	assert((NULL != rx_buffer)
	       && "object cannot be NULL");

	struct sockaddr_in originator_address_tmp1;
	socklen_t originator_address_size = sizeof(originator_address_tmp1);

	// Receive data from soscket
	const ssize_t rx_size = recvfrom(object->_rx_socket_fd,
					 rx_buffer, rx_bytes_read_size,
			   		 0,
					 (struct sockaddr *)&originator_address_tmp1,
					 &originator_address_size);

	// Catch recvfrom errors
	assert((0 <= rx_size)
	       && "Failed to receive packet from socket");

	// Convert originator address
	const struct virtualLinkSocketAddress originator_address_tmp2 = {
		.port = ntohs(originator_address_tmp1.sin_port),
		.ipv4_address = ntohl(originator_address_tmp1.sin_addr.s_addr),
	};

	// Ignore self-transmitted packets
	if (compareSocketAddress(&originator_address_tmp2, &object->_config.tx_socket_address)) {
		return 0;
	}

	// Not self-transmitted packet - update originator address
	if (NULL != originator_address) {
		*originator_address = originator_address_tmp2;
	}

	return (size_t)rx_size;
}

static void *rxProcessingThread(void *arg) {
	const struct virtualLinkObject *const object = arg;
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	struct virtualLinkSocketAddress originator_address;

	while (true) {
		// Wait for data
		int16_t read_size = virtualLink_receiveDataBlocking(object,
								    object->_config.rx_buffer,
								    object->_config.rx_buffer_size,
								    VIRTUAL_LINK_WAIT_FOREVER,
								    &originator_address);

		if (isRxInterruptEnabled(object) && (read_size > 0)) {
			callRxDoneCallback(object,
					   object->_config.rx_buffer, read_size,
					   &originator_address);
		}
	}
}

/* ----------------------------------------- Meta API ------------------------------------------ */
void virtualLink_Meta_processingLoop(const struct virtualLinkObject *const object) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	struct virtualLinkSocketAddress originator_address;

	if (isRxInterruptEnabled(object)) {
		const int16_t read_size =
			virtualLink_receiveDataBlocking(object,
						        object->_config.rx_buffer,
						        object->_config.rx_buffer_size,
						        VIRTUAL_LINK_DONT_WAIT,
							&originator_address);
		callRxDoneCallback(object,
				   object->_config.rx_buffer, read_size,
				   &originator_address);
	}
}

void virtualLink_Meta_runProcessingThread(const struct virtualLinkObject *const object) {
	pthread_t thread;
	pthread_create(&thread, NULL, rxProcessingThread, (void*)object);
}

/* -------------------------------------------- API -------------------------------------------- */
bool virtualLink_configFromStrings(struct virtualLinkConfig *const config,
				   const char *const interface_ipv4_address_string,
				   const char *const tx_socket_address_string,
				   const char *const rx_socket_address_string) {
	assert((NULL != config)
	       && "config cannot be NULL");
	assert((NULL != interface_ipv4_address_string)
	       && "interface_ipv4_address_string cannot be NULL");
	assert((NULL != tx_socket_address_string)
	       && "tx_socket_address_string cannot be NULL");
	assert((NULL != rx_socket_address_string)
	       && "rx_socket_address_string cannot be NULL");

	// Convert intertface address 
	const uint32_t interface_ipv4_address_number =
		(uint32_t)inet_addr(interface_ipv4_address_string);
	if (INADDR_NONE == interface_ipv4_address_number) {
		// Failed to convert interface_ipv4_address from string
		return false;
	}
	config->interface_ipv4_address = ntohl(interface_ipv4_address_number);

	// Convert tx socket address
	bool ret = socketAddressFromString(tx_socket_address_string,
					   &config->tx_socket_address.ipv4_address,
					   &config->tx_socket_address.port);
	if (!ret) {
		// Failed to convert tx_socket_address from string
		return false;
	}

	// Convert rx socket address
	ret = socketAddressFromString(rx_socket_address_string,
				      &config->rx_socket_address.ipv4_address,
				      &config->rx_socket_address.port);

	return ret;
}

void virtualLink_init(struct virtualLinkObject *const object,
		      const struct virtualLinkConfig *const config) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((NULL != config)
	       && "config cannot be NULL");

	copyConfig(object, config);

	// Create tx socket
	const struct sockaddr_in tx_socket_address = {
		.sin_family = AF_INET,
		.sin_addr = htonl(object->_config.tx_socket_address.ipv4_address),
		.sin_port = htons(object->_config.tx_socket_address.port),
	};

	object->_tx_socket_fd = initTxSocket(&tx_socket_address);

	// Create rx socket
	const struct sockaddr_in rx_socket_address = {
		.sin_family = AF_INET,
		.sin_addr = htonl(object->_config.rx_socket_address.ipv4_address),
		.sin_port = htons(object->_config.rx_socket_address.port),
	};

	object->_rx_socket_fd = initRxSocket(&rx_socket_address);

	// Create epoll and add rx socket as observable
	object->_epoll_descriptor = createEpoll();
	addObservableFileDescriptor(object->_epoll_descriptor, object->_rx_socket_fd, EPOLLIN);

	// Attach rx socket to multicast group
	const uint32_t interface_ipv4_address = htonl(object->_config.interface_ipv4_address);
	attachSocketToMulticastGroup(object->_rx_socket_fd,
				     interface_ipv4_address,
				     (uint32_t)rx_socket_address.sin_addr.s_addr);

	object->_is_rx_interrupt_enabled = false;
	object->_rx_done_callback.function = NULL;

	object->_is_initialized = true;
}

size_t virtualLink_sendDataBlocking(const struct virtualLinkObject *const object,
				    const void *const tx_data, size_t tx_data_size) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	// LOG_DBG("%s(data_size=%d)", (const char*)__PRETTY_FUNCTION__, tx_data_size);

	const struct sockaddr_in destination_address = {
		.sin_family = AF_INET,
		.sin_addr = htonl(object->_config.rx_socket_address.ipv4_address),
		.sin_port = htons(object->_config.rx_socket_address.port),
	};

	const ssize_t tx_size = sendto(object->_tx_socket_fd,
				       tx_data, tx_data_size,
				       0,
				       (struct sockaddr *)&destination_address,
			      	       sizeof(struct sockaddr_in));
	assert((0 <= tx_size )
	       && "Failed to send data");
	
	return (size_t)tx_size;
}

size_t virtualLink_receiveDataBlocking(const struct virtualLinkObject *const object,
				       void *const rx_buffer, size_t rx_bytes_read_size,
				       int timeout_ms,
				       struct virtualLinkSocketAddress *const originator_address) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	const uint32_t start_timestamp = systemTime_getFreezableEpochMs();

	while (true) {
		if (isRxDataAwaiting(object, 0)) {
			const size_t rx_size = receiveData(object,
							   rx_buffer, rx_bytes_read_size,
							   originator_address);
			assert((0 <= rx_size)
	       		       && "Failed to receive packet from socket");
			
			return rx_size;
		}

		if (0 == timeout_ms) {
			break;
		}

		if (0 < timeout_ms) {
			const uint32_t delta_time = systemTime_getFreezableEpochMs() - start_timestamp;
			if (delta_time >= timeout_ms) {
				break;
			}
		}
	}

	return 0;
}

void virtualLink_enableRxInterrupt(struct virtualLinkObject *const object, bool state) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");
	
	object->_is_rx_interrupt_enabled = state;
}

void virtualLink_registerRxDoneCallback(struct virtualLinkObject *const object,
					virtualLinkRxDoneCallbackFunction *function,
					void *user_data) {
	assert((NULL != object)
	       && "object cannot be NULL");
	assert((object->_is_initialized)
	       && "object has to be initialized");

	object->_rx_done_callback.function = function;
	object->_rx_done_callback.user_data = user_data;
}
