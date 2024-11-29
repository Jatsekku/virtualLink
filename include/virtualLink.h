#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define VIRTUAL_LINK_WAIT_FOREVER (-1)
#define VIRTUAL_LINK_DONT_WAIT (0)

struct virtualLinkSocketAddress {
	uint32_t ipv4_address;
	uint16_t port;
};

typedef void 
virtualLinkRxDoneCallbackFunction(const void *const rx_data, size_t rx_data_size,
				  const struct virtualLinkSocketAddress *const originator_address,
				  void *user_data);

struct virtualLinkConfig {
	struct virtualLinkSocketAddress tx_socket_address;
	struct virtualLinkSocketAddress rx_socket_address;
	uint32_t interface_ipv4_address;

	void *rx_buffer;
	size_t rx_buffer_size;
};

struct virtualLinkObject {
	struct virtualLinkConfig _config;

	int _tx_socket_fd;
	int _rx_socket_fd;
	int _epoll_descriptor;

	struct {
		virtualLinkRxDoneCallbackFunction *function;
		void *user_data;
	} _rx_done_callback;

	bool _is_initialized;
	bool _is_rx_interrupt_enabled;
};

/* ----------------------------------------- Meta API ------------------------------------------ */
/**
 * @brief Processing loop
 *        This function has to be called periodically to let module process internal data
 *	  It should be used only in single-threaded aplication.
 *	  Do not mix it with virtualLink_Meta_runProcessingThread(). 
 * @param[out] object Pointer to virtualLink object
 */
void virtualLink_Meta_processingLoop(const struct virtualLinkObject *const object);

void virtualLink_Meta_runProcessingThread(const struct virtualLinkObject *const object);

/* -------------------------------------------- API -------------------------------------------- */
/**
 * @brief Fill config structure by providing appropriate strings
 * 
 * @param[out] config Pointer to virtualLink configuration
 * @param[in] interface_ipv4_address_string String representin interface IPv4 address
 * @param[in] tx_socket_address_string String representing TX socket address
 * @param[in] rx_socket_address_string String representing RX socket address
 * @return Bool infroming if config has been successfully filled with data
 */
bool virtualLink_configFromStrings(struct virtualLinkConfig *const config,
				   const char *const interface_ipv4_address_string,
				   const char *const tx_socket_address_string,
				   const char *const rx_socket_address_string);

/**
 * @brief Init virtualLink object
 * 
 * @param[out] object Pointer to virtualLink object
 * @param[in] config Pointer to virtualLink configuration
 */
void virtualLink_init(struct virtualLinkObject *const object,
		      const struct virtualLinkConfig *const config);

/**
 * @brief Send data over virtualLink in blocking manner (no internal FIFO)
 * 
 * @param[in] object Pointer to virtualLink object
 * @param[in] tx_data Pointer to data that should be send
 * @param[in] tx_data_size The size of tx data
 *
 * @return Amount of bytes that has been sent
 */
size_t virtualLink_sendDataBlocking(const struct virtualLinkObject *const object,
				    const void *const tx_data, size_t tx_data_size);

/**
 * @brief Receive data over virtualLink in blocking manner (no internal FIFO)
 * 
 * @param[in] object Pointer to virtualLink object
 * @param[in] rx_buffer Pointer to buffer where incoming data should be stored
 * @param[in] rx_bytes_read_size Amount of bytes that should be read and stored into buffer
 * @param[in] timeout_ms Timeout for data reception, given in miliseconds
 * @param[out] originator_address Pointer to struct where data's originator will be stored
 *
 * @return Amount of bytes that has been received
 */
size_t virtualLink_receiveDataBlocking(const struct virtualLinkObject *const object,
				       void *const rx_buffer, size_t rx_bytes_read_size,
				       int timeout_ms,
				       struct virtualLinkSocketAddress *const originator_address);

/**
 * @brief Enable RX interrupt
 *
 * @param[in] object Pointer to virtualLink object
 * @param[in] state Bool determining if RX interrupt should be enabled or not 
 */
void virtualLink_enableRxInterrupt(struct virtualLinkObject *const object, bool state);

/**
 * @brief Register function that will be called when data will be received
 *
 * @param[in] object Pointer to virtualLink object
 * @param[in] function Pointer to callback function
 * @param[in] user_data Pointer to optional user data that will be passed to callback
 */
void virtualLink_registerRxDoneCallback(struct virtualLinkObject *const object,
					virtualLinkRxDoneCallbackFunction *function,
					void *user_data);

