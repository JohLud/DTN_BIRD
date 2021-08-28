#ifndef _BIRD_SCE_EXTENSION_H_
#define _BIRD_SCE_EXTENSION_H_

#include <stdio.h>

// EXTENSION
// build a simple checksum for a scheduled_contact_entry
// this is far too simple and error prone, but it is enough for the start
#define SCES_FILENAME	"sces.bin"
#define SCE_SIZE	15

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


u32 sce_signiture(scheduled_contact_entry entry);
scheduled_contact_entries * merge_sces(scheduled_contact_entries *entries1, scheduled_contact_entries *entries2);
void print_sces(scheduled_contact_entries *entries);

void store_sces(scheduled_contact_entries *entries);
void store_sce(FILE *fd, scheduled_contact_entry *entry, int * pos);
//void write_15_byte(FILE *fd, byte *data);
scheduled_contact_entries * load_sces(void);


#endif

