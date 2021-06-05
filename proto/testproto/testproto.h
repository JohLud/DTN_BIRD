
#ifndef _BIRD_TESTPROTO_H_
#define _BIRD_TESTPROTO_H_


#include "lib/socket.h"
#include "nest/bird.h"
#include "nest/route.h"


#endif

struct testproto_config {

};

struct testproto_socket {

};

struct testproto_conn {

};

struct testproto_proto {

};

/* Network parameters */

#define TESTPROTO_PORT		1234
#define TESTPROTO_VERSION		1
#define TESTPROTO_HEADER_LENGTH	?
#define TESTPROTO_MAX_MESSAGE_LENGTH	?
#define TESTPROTO_MAX_EXT_MSG_LENGTH	?
#define TESTPROTO_RX_BUFFER_SIZE	4096
#define TESTPROTO_TX_BUFFER_SIZE	4096
#define TESTPROTO_RX_BUFFER_EXT_SIZE	65535
#define TESTPROTO_TX_BUFFER_EXT_SIZE	65535

/* Packet types */

#define PKT_HELLO		0x01
