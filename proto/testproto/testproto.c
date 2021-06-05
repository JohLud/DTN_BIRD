#include <stdlib.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/string.h"

#include "testproto.h"


/**
 * testproto_open - open a testproto instance
 * @p: testproto instance
 *
 * This function allocates and configures shared testproto resources, mainly listening
 * sockets. Should be called as the last step during initialization (when lock
 * is acquired and neighbor is ready). When error, caller should change state to
 * PS_DOWN and return immediately.
 */
static int testproto_open(struct testproto_proto *p) { }

/**
 * testproto_close - close a testproto instance
 * @p: testproto instance
 *
 * This function frees and deconfigures shared testproto resources.
 */
static void testproto_close(struct testproto_proto *p) { }

static void testproto_startup(struct testproto_proto *p) { }
testproto
static void testproto_initiate(struct testproto_proto *p) { }

/**
 * testproto_close_conn - close a testproto connection
 * @conn: connection to close
 *
 * This function takes a connection described by the &testproto_conn structure, closes
 * its socket and frees all resources associated with it.
 */
void
testproto_close_conn(struct testproto_conn *conn)
{ }

static void
testproto_down(struct testproto_proto *p)
{ }

void
testproto_stop(struct testproto_proto *p, int subcode, byte *data, uint len)
{ }

static void
testproto_setup_conn(struct testproto_proto *p, struct testproto_conn *conn)
{ }

static void
testproto_setup_sk(struct testproto_conn *conn, sock *s)
{ }

/**
 * testproto_connect - initiate an outgoing connection
 * @p: testproto instance
 *
 * The testproto_connect() function creates a new &testproto_conn and initiates
 * a TCP connection to the peer. The rest of connection setup is governed
 * by the testproto state machine as described in the standard.
 */
static void
testproto_connect(struct testproto_proto *p)	/* Enter Connect state and start establishing connection */
{ }

static int
testproto_start(struct proto *P)
{ }

static int
testproto_shutdown(struct proto *P)
{ }

static struct proto *
testproto_init(struct proto_config *CF)
{ }

void
testproto_postconfig(struct proto_config *CF)
{ }

struct protocol proto_testproto = {
  .name = 		"testproto",
  .template = 		"testproto%d",
  .class =		PROTOCOL_TESTPROTO,
  .preference = 	DEF_PREF_TESTPROTO,
  .channel_mask =	NB_IP | NB_VPN | NB_FLOW,
  .proto_size =		sizeof(struct testproto_proto),
  .config_size =	sizeof(struct testproto_config),
  .postconfig =		testproto_postconfig,
  .init = 		testproto_init,
  .start = 		testproto_start,
  .shutdown = 		testproto_shutdown,
  .reconfigure = 	testproto_reconfigure,
  .copy_config = 	testproto_copy_config,
  .get_status = 	testproto_get_status,
  .get_attr = 		testproto_get_attr,
  .get_route_info = 	testproto_get_route_info,
  .show_proto_info = 	testproto_show_proto_info
};
