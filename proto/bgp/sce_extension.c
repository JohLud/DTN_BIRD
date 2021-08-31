#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "bgp.h"
#include "lib/unaligned.h"
#include "lib/timer.h"


/**
 * Register timers for every sce in the given entries.
 * One timer is registered for the start_time and the other one, when the
 * contact ends (start_time + duration)
 */
void register_sces(scheduled_contact_entries * entries) {
	for (int i = 0; i < entries->number_of_entries; i++) {
		scheduled_contact_entry * entry = (entries->entries+i);
		unsigned long begin = entry->start_time;
		unsigned long end = entry->start_time + entry->duration;
		register_timer(contact_begin, begin, entry);
		register_timer(contact_end, end, entry);
	}
}

/**
 * function: function to be called, either contact_begin or contact_end
 * when: in milliseconds since 01.01.1970 UTC when the function should be called
 */
timer * register_timer(void (*hook)(struct timer *), unsigned long when, scheduled_contact_entry * entry_data) {

	unsigned long firetime = convert_unixtime_to_secfromnow(when);
	timer * tm = tm_new_init(NULL, hook, entry_data, 0, 0);

	tm_start(tm, firetime*1000000);

	return tm;
}

void
contact_begin(timer *t) {
	log(L_INFO "sce_ext 27: Begin of contact!");
}

void
contact_end(timer *t) {
	log(L_INFO "sce_ext 32: End of contact!");

	// release resources
	tm_stop(t);
	rfree(t);
}

/**
 * Seconds since 01.01.1970 UTC (Unix Epoch) are converted to seconds since now.
 */
unsigned long convert_unixtime_to_secfromnow(unsigned long unixtime) {
	time_t current_time = time(NULL);
	return unixtime - current_time;
}


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

	// if there are new sces, register timers for them
	if (existing_sces) {
		all_sces = merge_sces(entries, existing_sces);

		// find the new entries that are not in the existing entries and register timers for them
		scheduled_contact_entries * new_entries = find_new_sces(entries, existing_sces);
		register_sces(new_entries);
	} else {
		all_sces = entries;

		// if all entries are new (they are because there weren't existing),
		// register new timers for every sce
		register_sces(entries);

	}

	FILE *fd = fopen(SCES_FILENAME, "w");

	for (int i = 0; i < all_sces->number_of_entries; i++) {
		store_sce(fd, (all_sces->entries+i));
	}

	fclose(fd);

	// not freed because the sces are referenced in the timer data structure
	// TODO: does this make sense since sces only points to sce?

//	free(entries);
//	if (existing_sces) free(existing_sces);
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

/**
 * Finds which sces from the first sces (param: new) are new i.e. not included in the second sces (param: existing)
 * and returns them.
 */
scheduled_contact_entries * find_new_sces(scheduled_contact_entries * new, scheduled_contact_entries * existing) {

	// first find out how many new sces exists to malloc
	int num_of_new = 0;

	for (int i = 0; i < new->number_of_entries; i++) {
		u32 sign_new = sce_signature(*(new->entries+i));
		_Bool newone = 1;

		// check signature of the entry against all existing signatures
		for (int j = 0; j < existing->number_of_entries; j++) {
			u32 sign_existing = sce_signature(*(existing->entries+j));
			if (sign_new == sign_existing) newone = 0;
		}

		// if newone is still 1, the sce is new
		if (newone) {
			num_of_new++;
		}
	}

	scheduled_contact_entry * entry_array = malloc(sizeof(scheduled_contact_entry) * num_of_new);

	// store the new entries in entry
	int pos = 0;
	for (int i = 0; i < new->number_of_entries; i++) {
		u32 sign_new = sce_signature(*(new->entries+i));
		_Bool newone = 1;

		for (int j = 0; j < existing->number_of_entries; j++) {
			u32 sign_existing = sce_signature(*(existing->entries+j));
			if (sign_new == sign_existing) newone = 0;
		}

		// if newone is still 1, the sce is new
		// pos is incremented to move array pointer forward
		if (newone) {
			*(entry_array+pos) = *(new->entries+i);
			pos++;
		}
	}

	scheduled_contact_entries * entries = malloc(sizeof(scheduled_contact_entries));
	entries->number_of_entries = num_of_new;
	entries->entries = entry_array;

	return entries;

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
	 * We test if signatures are identical.
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
		log(L_INFO "Entry %u:\n  =>  %u %u %u %u",
				i+1, (entries->entries+i)->start_time, (entries->entries+i)->duration,
				(entries->entries+i)->asn1, (entries->entries+i)->asn2);
	}
	log(L_INFO "===============\n");
}
