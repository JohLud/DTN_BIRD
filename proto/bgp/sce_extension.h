#ifndef _BIRD_SCE_EXTENSION_H_
#define _BIRD_SCE_EXTENSION_H_

#include <stdio.h>

#include "nest/route.h"

#define SCES_FILENAME	"sces.bin"
#define SCE_SIZE	22

/* Extension to specify one scheduled contact entry of a network
 * 	start_time: 	when will the network be reachable			32-Bit [seconds since 1.1.1970]
 * 	up_time:		how long will the network be reachable		16-Bit [seconds of open connection]
 * 	asn1:		the ASN of the first network					32-Bit [ASN]
 * 	gw1:		the network gateway in AS1 to connect to AS2	32-Bit [IPv4 address]
 * 	asn2:		the ASN of the second network					32-Bit [ASN]
 * 	gw2:		the network gateway in AS2 to connect to AS1	32-Bit [IPv4 address]
 *
 * 	Total size:	176 Bit , 22 Byte
 */
// see specification of scheduled_network_entry for the right data types!
typedef struct scheduled_contact_entry {
	u32 start_time;
	u16 duration;
	u32 asn1;
	u32 gw1;
	u32 asn2;
	u32 gw2;
//	 TODO: define makro for sce signature
} scheduled_contact_entry;

// set of multiple scheduled_contact_entry
typedef struct scheduled_contact_entries {
	int number_of_entries;
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

scheduled_contact_entries * find_new_sces(scheduled_contact_entries * new, scheduled_contact_entries * existing);
void register_sces(scheduled_contact_entries * entries, struct channel *c, struct bgp_proto * proto);
timer * register_timer(void (*hook)(struct timer *), unsigned long when, scheduled_contact_entry * entry_data, struct channel *c, struct bgp_proto * proto);
void contact_begin(timer *t);
void contact_end(timer *t);
unsigned long convert_unixtime_to_secfromnow(unsigned long unixtime);

u32 sce_signature(scheduled_contact_entry entry);
scheduled_contact_entries * merge_sces(scheduled_contact_entries *entries1, scheduled_contact_entries *entries2);
void print_sces(scheduled_contact_entries *entries);

void store_sces(scheduled_contact_entries *entries, struct channel *c, struct bgp_proto * proto);
void store_sce(FILE *fd, scheduled_contact_entry *entry);
//void write_15_byte(FILE *fd, byte *data);
scheduled_contact_entries * load_sces(void);


#endif

