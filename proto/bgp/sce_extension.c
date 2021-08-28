#include <stdio.h>
#include <sys/stat.h>

#include "bgp.h"
#include "lib/unaligned.h"


void store_sce(FILE *fd, scheduled_contact_entry *entry, int * pos) {
	fwrite(entry, sizeof(*entry), 1, fd);
	return;

	/*
	log(L_INFO "sce_ext 50: MARK2");
	byte * data = malloc(SCE_SIZE);
	log(L_INFO "sce_ext 50: MARK2.1");
	put_u32(data + *pos, entry->start_time);
	*pos += 4;
	put_u16(data + *pos, entry->duration);		log(L_INFO "sce_ext 50: MARK2.2");
	*pos += 2;									log(L_INFO "sce_ext 50: MARK2.3");
	put_u32(data + *pos, entry->asn1);			log(L_INFO "sce_ext 50: MARK2.4");
	*pos += 4;									log(L_INFO "sce_ext 50: MARK2.5 asn2: %x pos: %x value: %x value-4: %x", entry->asn2, *pos, *(data + *pos), *(data + *pos - 4));
	put_u32(data + *pos, entry->asn2);			log(L_INFO "sce_ext 50: MARK2.6");
	*pos += 4;									log(L_INFO "sce_ext 50: MARK2.7");
	char nl = 0x0a;		// ascii char for newline "\n"
	put_u8(data + *pos, nl);					log(L_INFO "sce_ext 50: MARK2.8");
	*pos += 1;
	log(L_INFO "sce_ext 50: MARK3");
	size_t result = fwrite(data, SCE_SIZE, 1, fd);
	log(L_INFO "sce_ext 50: MARK4");
	free(data);*/
}

void store_sces(scheduled_contact_entries *entries) {

	scheduled_contact_entries * existing_sces = load_sces();
	scheduled_contact_entries * all_sces;

	if (existing_sces) {
		all_sces = merge_sces(entries, existing_sces);
	} else {
		all_sces = entries;
	}

	FILE *fd = fopen(SCES_FILENAME, "w");
	int pos = 0;
	for (int i = 0; i < all_sces->number_of_entries; i++) {
		store_sce(fd, (all_sces->entries+i), &pos);
	}
	fclose(fd);
	free(entries);
	if (existing_sces) free(existing_sces);
//	if (all_sces) free(all_sces);
}

scheduled_contact_entries * load_sces(void) {

	struct stat fileinfo;
	int exists = stat(SCES_FILENAME, &fileinfo);

	// check if file exists
	if (exists != 0) {
		return NULL;
	}

	int filesize = fileinfo.st_size;
//	int num_of_entries = filesize / 15;

	int num_of_entries = filesize / sizeof(scheduled_contact_entry);
	byte * data = malloc(filesize);

	FILE *fd = fopen(SCES_FILENAME, "rb");
//	size_t result = fread(data, filesize, 1, fd);


	/*
	 * Testing area
	 */


	scheduled_contact_entry * entry = malloc(sizeof(scheduled_contact_entry) * num_of_entries);

	fread(entry, sizeof(scheduled_contact_entry) * num_of_entries, 1, fd);


	fclose(fd);

	scheduled_contact_entries * sce = malloc( sizeof(scheduled_contact_entries) );
	sce->entries = entry;
	sce->number_of_entries = num_of_entries;
	return sce;



	/*
	scheduled_contact_entry * sce = malloc( sizeof(scheduled_contact_entry) * num_of_entries);

	int pos = 0;
	for (int i = 0; i < num_of_entries; i++) {
		u32 start_time = get_u32(data + pos);
		pos += 4;
		u16 duration = get_u16(data + pos);
		pos += 2;
		u32 asn1 = get_u32(data + pos);
		pos += 4;
		u32 asn2 = get_u32(data + pos);
		pos += 4;
		// newline 1 Byte Separator
		pos += 1;

		(sce+i)->start_time = start_time;
		(sce+i)->duration = duration;
		(sce+i)->asn1 = asn1;
		(sce+i)->asn2 = asn2;
	}

//	free(data);
	scheduled_contact_entries * sces = malloc( sizeof(scheduled_contact_entries) );

	sces->number_of_entries = num_of_entries;
	sces->entries = sce;

	return sces;
	*/
}



// build a simple checksum for a scheduled_contact_entry for checking duplicates
// this is far too simple and error prone, but it is enough for the start
//	 TODO: define makro for sce signature
u32 sce_signiture(scheduled_contact_entry entry) {
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
		u32 signiture1 = sce_signiture(*(entries1->entries+i));
		for (int j = 0; j < entries2->number_of_entries; j++) {
			u32 signiture2 = sce_signiture(*(entries2->entries+j));
			if (signiture1 == signiture2) {
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
		u32 signiture1 = sce_signiture(*(entries2->entries+i));
		// compare signature of entry2 against every signiture of entry1
		for (int j = 0; j < entries1->number_of_entries; j++) {
			u32 signiture2 = sce_signiture(*(entries1->entries+j));
			if (signiture1 == signiture2) {
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
