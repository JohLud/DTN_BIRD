#include <stdio.h>
#include <sys/stat.h>

#include "bgp.h"
#include "lib/unaligned.h"


/*
 * Stores an sce struct in a file pointed by fd.
 * The struct is stored raw and not there separate values.
 */
void store_sce(FILE *fd, scheduled_contact_entry *entry) {
	fwrite(entry, sizeof(*entry), 1, fd);
}

/*
 * Stores all sces in entries in a file named SCES_FILENAME.
 */
void store_sces(scheduled_contact_entries *entries) {

	// merge existing sces with the new ones to store all together
	scheduled_contact_entries * existing_sces = load_sces();
	scheduled_contact_entries * all_sces;

	if (existing_sces) {
		all_sces = merge_sces(entries, existing_sces);
	} else {
		all_sces = entries;
	}

	FILE *fd = fopen(SCES_FILENAME, "w");

	for (int i = 0; i < all_sces->number_of_entries; i++) {
		store_sce(fd, (all_sces->entries+i));
	}

	fclose(fd);
	free(entries);
	if (existing_sces) free(existing_sces);
//	if (all_sces) free(all_sces);
}

/*
 * Load all sces that are stored in SCES_FILENAME and return a pointer pointing to them.
 */
scheduled_contact_entries * load_sces(void) {

	// check if file exists and how large it is
	struct stat fileinfo;
	int exists = stat(SCES_FILENAME, &fileinfo);

	if (exists != 0) {
		return NULL;
	}

	int filesize = fileinfo.st_size;

	int num_of_entries = filesize / sizeof(scheduled_contact_entry);

	FILE *fd = fopen(SCES_FILENAME, "rb");

	scheduled_contact_entry * entry = malloc(sizeof(scheduled_contact_entry) * num_of_entries);

	// read number of sces from the file
	fread(entry, sizeof(scheduled_contact_entry) * num_of_entries, 1, fd);

	fclose(fd);

	scheduled_contact_entries * sce = malloc( sizeof(scheduled_contact_entries) );
	sce->entries = entry;
	sce->number_of_entries = num_of_entries;
	return sce;
}



// build a simple checksum for a scheduled_contact_entry for checking duplicates
// this is far too simple and error prone, but it is enough for the start
//	 TODO: define makro for sce signature
u32 sce_signature(scheduled_contact_entry entry) {
	return entry.start_time + entry.duration + entry.asn1 + entry.asn2;
}

scheduled_contact_entries * merge_sces(scheduled_contact_entries *entries1, scheduled_contact_entries *entries2)
{
	scheduled_contact_entries * entries = malloc(sizeof(scheduled_contact_entries));
	int num_of_entries = 0;
	int duplicates = 0;
	/*
	 * For initializing the scheduled_contact_entry array we need to calculate the size.
	 * We test if signatures are identical by XORing their values.
	 */
	for (int i = 0; i < entries1->number_of_entries; i++) {
		u32 signature1 = sce_signature(*(entries1->entries+i));
		for (int j = 0; j < entries2->number_of_entries; j++) {
			u32 signature2 = sce_signature(*(entries2->entries+j));
			if (signature1 == signature2) {
				duplicates++;
			}
		}
	}
	num_of_entries = entries1->number_of_entries + entries2->number_of_entries - duplicates;
	scheduled_contact_entry * entry_array = malloc(sizeof(scheduled_contact_entry)*num_of_entries);
	/*
	 * first add every entry of entries1
	 * and later add every entry of entries2, but
	 * check is the same sce_signature is inserted yet
	 * This presupposes that there are no duplicates in entry1
	 */
	// TODO: Pass value and assign to dereferenced pointer and not pointer to pointer
	int index = 0;
	for (int i = 0; i < entries1->number_of_entries; i++) {
		*(entry_array+i) = *(entries1->entries+i);
		index++;
	}
	for (int i = 0; i < entries2->number_of_entries; i++) {
		// only add the entries, that are not added yet, check added entries from entries1
		_Bool duplicate = 0;
		u32 signature1 = sce_signature(*(entries2->entries+i));
		// compare signature of entry2 against every signature of entry1
		for (int j = 0; j < entries1->number_of_entries; j++) {
			u32 signature2 = sce_signature(*(entries1->entries+j));
			if (signature1 == signature2) {
				duplicate = 1;	// if signature of entry1 & entry2 is identical --> duplicate --> entry2 not added
			}
		}
		if (!duplicate) {
			// FIX assignment
			*(entry_array+index) = *(entries2->entries+i);
			index++;
		}
	}
	entries->number_of_entries = num_of_entries;
	entries->entries = entry_array;
	// TODO: free entries1 and entries2?

	return entries;
}

void print_sces(scheduled_contact_entries *entries) {
	log(L_INFO "===============\nPrinting %u scheduled contact entries.", entries->number_of_entries);
	for (int i = 0; i < entries->number_of_entries; i++) {
		log(L_INFO "Entry %u:\n  =>  %x %x %x %x",
				i+1, (entries->entries+i)->start_time, (entries->entries+i)->duration,
				(entries->entries+i)->asn1, (entries->entries+i)->asn2);
	}
	log(L_INFO "===============\n");
}
