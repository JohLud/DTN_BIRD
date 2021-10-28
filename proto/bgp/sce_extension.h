/*
 * The main header file where the BGP extension sits.
 *
 * Written by Johannes Ludwig as a part of
 * the bachelor thesis at
 * Technische Universität Dresden
 * Faculty of Computer Science
 * Chair of Computer Networks
 * 2021
 *
 * The code contains Apache 2.0 licensed code from
 * Stanislav Ovsiannikov.
 * The author and the begin of the external code is
 * mentioned at the section, where the code begins.
 *
 * Code changes in the BIRD source code are marked with
 * a comment that declares the extended code.
 */

#ifndef _BIRD_SCE_EXTENSION_H_
#define _BIRD_SCE_EXTENSION_H_

#include <stdio.h>

#include "nest/route.h"

#define SCES_FILENAME	"sces.bin"
#define SCE_SIZE	32
#define DTNEPOCH 946684800000	// milliseconds since UNIX epoch to 01.01.2000 (UTC)

/* Extension to specify one scheduled contact entry of a network
 * 	start_time: 	when will the network be reachable			64-Bit [milliseconds since 01.01.2000 (UTC)]
 * 	up_time:		how long will the network be reachable		64-Bit [duration of possible contact in milliseconds]
 * 	asn1:		the ASN of the first network					32-Bit [ASN]
 * 	gw1:		the network gateway in AS1 to connect to AS2	32-Bit [IPv4 address]
 * 	asn2:		the ASN of the second network					32-Bit [ASN]
 * 	gw2:		the network gateway in AS2 to connect to AS1	32-Bit [IPv4 address]
 *
 * 	Total size:	176 Bit , 22 Byte
 */
// see specification of scheduled_network_entry for the right data types!
typedef struct scheduled_contact_entry {
	u64 start_time;
	u64 duration;
	u32 asn1;
	u32 gw1;
	u32 asn2;
	u32 gw2;
//	 TODO: define makro for sce signature
} scheduled_contact_entry;

// set of multiple scheduled_contact_entry
typedef struct scheduled_contact_entries {
	u16 number_of_entries;
	scheduled_contact_entry *entries;
} scheduled_contact_entries;

// composite type to pass sce and a channel to access the routing table when timer fires
typedef struct entry_data {
	scheduled_contact_entry * sce;
	struct channel * ch;
	struct bgp_proto * proto;
} entry_data;

// composite type to pass sce and an channel to access the routingtable when timer fires
typedef struct attrs_holding {
	struct eattr * attrs;
	u8 num_of_new;
} attrs_holding;


_Bool path_contains_as_pair(scheduled_contact_entry * entry, eattr * as_patah_attr, u32 mypublicasn);

// should be deleted later, only for debugging:
void print_nexthop(rte * rt);

void modify_routingtable_add(entry_data *ed);
void modify_routingtable_remove(entry_data *ed);
attrs_holding * insert_sce_in_path(scheduled_contact_entry * entry, struct eattr * attr, rte * routes, u32 mypublicasn);
attrs_holding * remove_duplicates(attrs_holding * attr_h);
_Bool check_equal_path(u32 * path1, u8 len_path1, u32 * path2, u8 len_path2);
u32 * get_as_path(struct eattr * as_path_attr);
u32 * extend_as_path(u32 * as_path, u8 index, u8 num_segments, u32 asn);
u32 * kick_first_segment(u32 * as_path, u8 num_segments);
u32 * add_first_segment(u32 * as_path, u8 num_segments, u32 asn);
attrs_holding * search_for_tail(u32 * as_path, u8 position, u8 num_of_segments, rte * routes);
struct eattr * get_as_path_attr(rte * route);
eattr * merge_head_tail(u32 * as_path1, u8 pos1, u32 * as_path2, u8 pos2, u8 length_of_2);
eattr * build_attr(u32 * as_path, u8 sizeofpath);
void print_as_path(u32 * path, u8 length);
void print_as_path_rt(rte * r);
void print_rte_infos(rte * r);
ea_list * add_nexthop_attribute(struct nexthop * nh, ea_list * eal);
rte * copy_rte_and_insert_as_path(rte ** rt, struct eattr * new_as_path, struct bgp_proto * p, scheduled_contact_entry * entry);
void add_next_hop(rta * att, struct bgp_proto * p, scheduled_contact_entry * entry);

_Bool is_unique_route(rte * route, rtable * table, u32 * ipv4_address);

scheduled_contact_entries * find_new_sces(scheduled_contact_entries * new, scheduled_contact_entries * existing);
void register_sces(scheduled_contact_entries * entries, struct channel *c, struct bgp_proto * proto);
timer * register_timer(void (*hook)(struct timer *), u64 when, scheduled_contact_entry * entry_data, struct channel *c, struct bgp_proto * proto);
void contact_begin(timer *t);
void contact_end(timer *t);
u64 convert_unixtime_to_secfromnow(u64 relative_time);

_Bool check_equal_sces(scheduled_contact_entry * entry1, scheduled_contact_entry * entry2);
scheduled_contact_entries * merge_sces(scheduled_contact_entries *entries1, scheduled_contact_entries *entries2);
void print_sces(scheduled_contact_entries *entries);

void store_sces(scheduled_contact_entries *entries, struct channel *c, struct bgp_proto * proto);
void store_sce(FILE *fd, scheduled_contact_entry *entry);
//void write_15_byte(FILE *fd, byte *data);
scheduled_contact_entries * load_sces(void);

unsigned char * get_sces_cbor(unsigned int * data_size);

/*
 * Functions for CBOR support.
 * The following code is made by Stanislav Ovsiannikov
 * from https://github.com/naphaso/cbor-c .
 * The Code is licensed under the Apache License 2.0
 * URL to license: https://github.com/naphaso/cbor-c/blob/master/LICENSE
 * The following code was not modified.
 */
#define CBOR_TOKEN_TYPE_INT 1
#define CBOR_TOKEN_TYPE_LONG 2
#define CBOR_TOKEN_TYPE_MAP 3
#define CBOR_TOKEN_TYPE_ARRAY 4
#define CBOR_TOKEN_TYPE_STRING 5
#define CBOR_TOKEN_TYPE_BYTES 6
#define CBOR_TOKEN_TYPE_TAG 7
#define CBOR_TOKEN_TYPE_SPECIAL 8

#define CBOR_TOKEN_TYPE_INCOMPLETE 1000
#define CBOR_TOKEN_TYPE_ERROR 2000

struct cbor_token {
    unsigned int type;
    int sign;
    unsigned int int_value;
    unsigned long long long_value;
    char *string_value;
    unsigned char *bytes_value;
    const char *error_value;
};

unsigned int cbor_read_token(unsigned char *data, unsigned int size, unsigned int offset, struct cbor_token *token);
unsigned char *cbor_write_plong(unsigned char *data, unsigned int size, unsigned long long value);
unsigned char *cbor_write_pint(unsigned char *data, unsigned int size, unsigned int value);
unsigned char *cbor_write_type_size(unsigned char *data, unsigned int size, unsigned int type, unsigned int type_size);
unsigned char *cbor_write_type_long_size(unsigned char *data, unsigned int size, unsigned int type, unsigned long long type_size);
unsigned char *cbor_write_uint(unsigned char *data, unsigned int size, unsigned int value);
unsigned char *cbor_write_ulong(unsigned char *data, unsigned int size, unsigned long long value);
unsigned char *cbor_write_int(unsigned char *data, unsigned int size, int value);
unsigned char *cbor_write_long(unsigned char *data, unsigned int size, long long value);
unsigned char *cbor_write_array(unsigned char *data, unsigned int size, unsigned int array_size);

#endif

