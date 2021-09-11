#ifndef _BIRD_SCE_EXTENSION_H_
#define _BIRD_SCE_EXTENSION_H_

#include <stdio.h>

#include "nest/route.h"

//#include "nest/protocol.h"

// EXTENSION
// build a simple checksum for a scheduled_contact_entry
// this is far too simple and error prone, but it is enough for the start
#define SCES_FILENAME	"sces.bin"
//#define SCE_SIZE	15

/* Extension to specify one scheduled contact entry of a network
 * 	start_time: 	when will the network be reachable		32-Bit seconds since 1.1.1970
 * 	up_time:		how long will the network be reachable	16-Bit seconds of open connection
 * 	asn1:		the ASN of the first network				32-Bit
 * 	asn2:		the ASN of the second network				32-Bit
 *
 * 	Total size:	112 Bit , 14 Byte
 */
// see specification of scheduled_network_entry for the right data types!
typedef struct scheduled_contact_entry {
	u32 start_time;
	u16 duration;
	u32 asn1;
	u32 asn2;
//	 TODO: define makro for sce signature
} scheduled_contact_entry;

// set of multiple scheduled_contact_entry
typedef struct scheduled_contact_entries {
	int number_of_entries;
	scheduled_contact_entry *entries;
} scheduled_contact_entries;

// compoite type to pass sce and an channel to access the routingtable when timer fires
typedef struct entry_data {
	scheduled_contact_entry * sce;
	struct channel * ch;
} entry_data;

// compoite type to pass sce and an channel to access the routingtable when timer fires
typedef struct attrs_holding {
	struct eattr * attrs;
	u8 num_of_new;
} attrs_holding;



void modify_routingtable(entry_data *ed);
attrs_holding * insert_sce_in_path(scheduled_contact_entry * entry, struct eattr * attr, rte * routes);
u32 * get_as_path(struct eattr * as_path_attr, u8 num_of_segments);
u32 * extend_as_path(u32 * as_path, u8 index, u8 num_segments, u32 asn);
attrs_holding * search_for_tail(u32 * as_path, u8 position, u8 num_of_segments, rte * routes);
struct eattr * get_as_path_attr(rte * route);
eattr * merge_head_tail(u32 * as_path1, u8 pos1, u32 * as_path2, u8 pos2, u8 length_of_2);
eattr * build_attr(u32 * as_path, u8 sizeofpath);
void print_as_path(u32 * path, u8 length);
void print_rte_infos(rte * r);
rte * copy_rte_and_insert_as_path(rte * rt, struct eattr * new_as_path);

scheduled_contact_entries * find_new_sces(scheduled_contact_entries * new, scheduled_contact_entries * existing);
void register_sces(scheduled_contact_entries * entries, struct channel *c);
timer * register_timer(void (*hook)(struct timer *), unsigned long when, scheduled_contact_entry * entry_data, struct channel *c);
void contact_begin(timer *t);
void contact_end(timer *t);
unsigned long convert_unixtime_to_secfromnow(unsigned long unixtime);

u32 sce_signature(scheduled_contact_entry entry);
scheduled_contact_entries * merge_sces(scheduled_contact_entries *entries1, scheduled_contact_entries *entries2);
void print_sces(scheduled_contact_entries *entries);

void store_sces(scheduled_contact_entries *entries, struct channel *c);
void store_sce(FILE *fd, scheduled_contact_entry *entry);
//void write_15_byte(FILE *fd, byte *data);
scheduled_contact_entries * load_sces(void);


#endif

