/*
 * The main source file where the BGP extension sits.
 * Here we calculate new paths, set timers, save and
 * load scheduled contact entries to/from the filesystem
 * and trigger route deletions.
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


#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "bgp.h"
#include "lib/unaligned.h"
#include "lib/timer.h"
#include "nest/protocol.h"
#include "nest/route.h" // for rte_better
#include "nest/iface.h" // for neighbor
#include <inttypes.h> // for printing u64



/**
 * Builds the AS_PATH attribute as eattr
 * @as_path: path of ASN
 * @sizeofpath: how many segements does the path has
 */
eattr * build_attr(u32 * as_path, u8 sizeofpath) {

	// the path does not contain the own ASN
	as_path = kick_first_segment(as_path, --sizeofpath);

	eattr * new_attr = malloc(sizeof(eattr));
	adata * new_data = malloc(sizeof(adata) + (4*sizeofpath));

	new_data->length = sizeofpath*4 + 2;
	new_data->data[0] = 2;
	new_data->data[1] = sizeofpath;

	int pos = 0;
	for (int i = 2; i < (sizeofpath*4 + 2); i += 4) {
		put_u32( (new_data->data+i), as_path[pos] );
		pos++;
	}

	new_attr->id = 770;
	new_attr->flags = 0x40;
	new_attr->type = 6;

	new_attr->u.ptr = new_data;

	return new_attr;
}

/**
 * Build a next hop attribute and append it to an ea_list
 *
 * @nh: next hop
 * @eal: list to append
 *
 * This function is not used, but could be neccessary
 */
ea_list * add_nexthop_attribute(struct nexthop * nh, ea_list * eal) {

	struct adata * new_attrdata = malloc( 32 );
	new_attrdata->length = 10;

	ip_addr *nh_addr = (void *) new_attrdata->data;
	nh_addr[0] = nh->gw;
	nh_addr[1] = IPA_NONE;

	ea_list * new_list = malloc(sizeof(ea_list) + sizeof(eattr)*2);
	eattr * new_attr = &(new_list->attrs[0]);

	new_list->flags = EALF_SORTED;
	new_list->count = 1;
	new_list->next = eal;

	new_attr->id = 771;
	new_attr->type = 0x4;
	new_attr->flags = 0;
	new_attr->u.ptr = new_attrdata;

	return new_list;
}

/**
 * If we added an ASN segment to a path, we must complete
 * the path from there on with the second given path.
 *
 * @as_path1: the path that was build
 * @pos1: the position in as_path1 from where the completion has to be done
 * @as_path2: the path that completes as_path1
 * @pos2: the position in as_path2 from where the segments are inserted in as_path1
 * @length_of_2: the total segment length of as_path2
 *
 */
eattr * merge_head_tail(u32 * as_path1, u8 pos1, u32 * as_path2, u8 pos2, u8 length_of_2) {
	int MAX_as_length = (pos1 + length_of_2) * 4;
	u32 * new_as_path = malloc(MAX_as_length);


	int pos = 0;
	for (int i = 0; i <= pos1; i++) {
		new_as_path[i] = as_path1[i];
		pos++;
	}

	for (int i = pos2 + 1; i < length_of_2; i++) {
		new_as_path[pos] = as_path2[i];
		pos++;
	}

	eattr * new_attr = build_attr(new_as_path, pos);

	return new_attr;
}

/**
 * Searches for paths, that can complete the build-path with the appended
 * ASN from the scheduled contact entry
 *
 * @as_path: path that must be completed
 * @position: the position from where on the path must be completed
 * @num_of_segments: the total segment length of the build-path
 * @routes: all routes, where we search for paths to complete the build-path
 */
attrs_holding * search_for_tail(u32 * as_path, u8 position, u8 num_of_segments, rte * routes) {
	u32 asn1 = as_path[position];
	u32 search_asn = as_path[position + 1];

	u8 count_new_paths = 0;

	// count how many new routes are found
	rte * current_route;
	for (current_route = routes; current_route; current_route = current_route->next) {

		struct eattr * path_attr = get_as_path_attr(current_route);
		if (!(path_attr)) continue;

		int num_of_segments_tmp = path_attr->u.ptr->data[1];
		u32 * segments_search_in = get_as_path(path_attr);
		for (int i = 0; i < num_of_segments_tmp; i++) {
			if (segments_search_in[i] == search_asn
					&& segments_search_in[i+1] != asn1) {
				count_new_paths++;
			}
		}
	}

	if (count_new_paths == 0) return NULL;

	eattr * new_attrs = malloc(sizeof(eattr) * count_new_paths);
	int position_attrs = 0;

	// for every tail a new eattr is build
	for (current_route = routes; current_route; current_route = current_route->next) {

		struct eattr * path_attr = get_as_path_attr(current_route);
		if (!(path_attr)) continue;

		int num_of_segments_tmp = path_attr->u.ptr->data[1];
		u32 * segments_search_in = get_as_path(path_attr);

		for (int i = 0; i < num_of_segments_tmp; i++) {
			if (segments_search_in[i] == search_asn
					&& segments_search_in[i+1] != asn1) {
				eattr * new_attr = merge_head_tail(as_path, position + 1, segments_search_in, i, num_of_segments_tmp);
				*(new_attrs+position_attrs) = *(new_attr);
				position_attrs++;
			}
		}
	}

	attrs_holding * new_holding = malloc(sizeof(attrs_holding));
	new_holding->num_of_new = count_new_paths;
	new_holding->attrs = new_attrs;

	return new_holding;
}

/**
 * Add an ASN to the path at the given index
 *
 * @as_path: path that is to be extended by an ASN
 * @index: to position to insert the ASn
 * @num_segments: total segment length of the as_path
 * @asn: ASN that is inserted
 */
u32 * extend_as_path(u32 * as_path, u8 index, u8 num_segments, u32 asn) {
	u32 * new_as_path = malloc(num_segments * 4);

	u8 border = index + 1;
	for (int i = 0 ; i < num_segments; i++) {
		new_as_path[i] = as_path[i];
		if (i >= border) {
			new_as_path[i] = as_path[i-1];
		}
	}

	new_as_path[border] = asn;

	free(as_path);
	return new_as_path;
}

/**
 * Add an ASN to the first position of an as_path.
 * This is done when the own ASN is added to the path.
 *
 * @as_path: the path that is extended
 * @num_segments: total segment length of the as_path
 * @asn: the asn that is inserted
 */
u32 * add_first_segment(u32 * as_path, u8 num_segments, u32 asn) {
	u32 * new_as_path = malloc(num_segments * 4);

	for (int i = 1 ; i < num_segments; i++) {
		new_as_path[i] = as_path[i-1];
	}

	new_as_path[0] = asn;

	free(as_path);
	return new_as_path;
}

/**
 * The first element is deleted from the path
 *
 * @as_path: the path
 * @num_segments: total segment length of the as_path
 */
u32 * kick_first_segment(u32 * as_path, u8 num_segments) {
	u32 * new_as_path = malloc(num_segments * 4);

	for (int i = 0 ; i < num_segments; i++) {
		new_as_path[i] = as_path[i+1];
	}

	free(as_path);
	return new_as_path;
}

/**
 * Extract the the as path from an eattr.
 * The as path is an array of u32.
 *
 * @as_path_attr: the attribute where the as path is included
 */
u32 * get_as_path(struct eattr * as_path_attr) {
	u8 num_of_segments = (as_path_attr->u.ptr->length - 2)/4;
	u32 * asns = malloc(num_of_segments * 4);

	// first byte is type and second num. of segments
	int pos = 2;

	for (int i = 0; i < num_of_segments; i++) {
		u32 segment = get_u32( as_path_attr->u.ptr->data + pos );
		pos += 4;

		asns[i] = segment;
	}
	return asns;
}

/**
 * Takes an u32 array and prints it to the console.
 *
 * @path: the as path
 * @length: total segment length of the as_path
 */
void print_as_path(u32 * path, u8 length) {
	log(L_INFO "===	AS_Path:");
	for (int i = 0; i < length; i++) {
		log(L_INFO "AS_SEGMENT: %u", *(path+i) );
	}
	log(L_INFO "===	END AS_Path");
}

/**
 * Takes a route, extracts the path attribute, extracts the as path and prints it.
 *
 * @r: the route, where the as path is included
 */
void print_as_path_rt(rte * r) {
	eattr * tmp_a = get_as_path_attr(r);
	if (!(tmp_a)) {
		log(L_INFO "No path attribute in route!");
		return;
	}
	u32 * tmp_p = get_as_path(tmp_a);
	print_as_path(tmp_p, tmp_a->u.ptr->data[1]);
}

/**
 * Compare the segments of two paths and return 1 if they are identical and 0 otherwise.
 *
 * @path1: the first as path
 * @len_path1: total segment length of path1
 * @path2: the second as path
 * @len_path2: total segment length of path2
 */
_Bool check_equal_path(u32 * path1, u8 len_path1, u32 * path2, u8 len_path2) {

	if (len_path1 != len_path2) return 0;
	for (int i = 0 ; i < len_path1; i++) {
		if ( *(path1+i) != *(path2+i) ) return 0;
	}
	return 1;
}

/**
 * Takes a set of eattr's and removes all duplicate paths.
 *
 * @attr_h: the attrs_holding to search in
 */
attrs_holding * remove_duplicates(attrs_holding * attr_h) {

	if (attr_h->num_of_new < 2) return NULL;

	for (int i = 0 ; i < attr_h->num_of_new ; i++) {

		eattr * curr_attr = attr_h->attrs+i;
		u32 * curr_path = get_as_path(curr_attr);
		u8 curr_path_len = curr_attr->u.ptr->data[1];

		for (int y = 0 ; y < attr_h->num_of_new ; y++) {

			if (i == y) continue;
			eattr * tmp_attr = attr_h->attrs+y;
			u32 * tmp_path = get_as_path(tmp_attr);
			u8 tmp_path_len = tmp_attr->u.ptr->data[1];

			_Bool same = 0;
			// check if the paths are equal
			same = check_equal_path(curr_path, curr_path_len, tmp_path, tmp_path_len);

			// remove the corresponding eattr from the holding if they are equal
			if (same) {
				attr_h->num_of_new--;
				for (int del_i = i; del_i < attr_h->num_of_new; del_i++) {
					if (attr_h->attrs+del_i + 1) {
						*(attr_h->attrs+del_i) = *(attr_h->attrs+del_i+1);
					}
				}
			}
		}
	}
}

/**
 * Takes an eattr and inserts the AS-AS pair from the scheduled contact entry.
 * If added, it searches for paths to complete the build-path from the position of the insertion on.
 *
 * @entry: the schedued contact entry, where the AS-AS pair is stored
 * @attr: the AS_PATH attribute of a route
 * @routes: all routes that lead to a network
 * @mypublicasn: the own ASN
 */
struct attrs_holding * insert_sce_in_path(scheduled_contact_entry * entry, struct eattr * attr, rte * routes, u32 mypublicasn) {
	// TODO: Currently only supports ASN4: 4 byte asn numbers. Do I need support for 2 Byte ASN's?
	u32 asn1 = entry->asn1;
	u32 asn2 = entry->asn2;

	u8 num_of_segments = attr->u.ptr->data[1];

	if (num_of_segments < 2) return NULL;

	// get path from eattr
	u32 * as_path = get_as_path(attr);

	// add own asn to path
	as_path = add_first_segment(as_path, ++num_of_segments, mypublicasn);

	// possible new path
	struct eattr * new_attrs;
	int num_of_new_attrs = 0;

	// count how many new paths i.e. eattrs are build
	for (int segment_index = 0; segment_index < num_of_segments; segment_index++) {
		if (asn1 == as_path[segment_index]) {
			// stop if asn pair is included already
			if (asn2 == as_path[segment_index+1]) break;
			// extend as_path to add asn2
			num_of_segments++;
			as_path = extend_as_path(as_path, segment_index, num_of_segments, asn2);

			attrs_holding * result = search_for_tail(as_path, segment_index, num_of_segments, routes);

			if ( result ) {
				num_of_new_attrs += result->num_of_new;
			}
			break;
		}
		else if (asn2 == as_path[segment_index]) {
			// stop if asn pair is included already
			if (asn1 == as_path[segment_index+1]) break;
			// extend as_path to add asn1
			num_of_segments++;
			as_path = extend_as_path(as_path, segment_index, num_of_segments, asn1);

			attrs_holding * result = search_for_tail(as_path, segment_index, num_of_segments, routes);

			if ( result ) {
				num_of_new_attrs += result->num_of_new;
			}
			break;
		}
	}

	if (num_of_new_attrs == 0) return NULL;

	new_attrs = malloc(sizeof(struct eattr) * num_of_new_attrs);
	int attr_index = 0;

	// reset changes to as_path
	free(as_path); // is extended from count section
	num_of_segments = attr->u.ptr->data[1];
	as_path = get_as_path(attr);

	as_path = add_first_segment(as_path, ++num_of_segments, mypublicasn);

	// add new paths as eattr
	for (int i = 0; i < num_of_segments; i++) {
		if (asn1 == as_path[i]) {
			// stop if asn pair is included already
			if (asn2 == as_path[i+1]) break;
			// extend as_path to add asn2
			num_of_segments++;
			as_path = extend_as_path(as_path, i, num_of_segments, asn2);

			attrs_holding * result = search_for_tail(as_path, i, num_of_segments, routes);
			if (result->num_of_new > 0) {
				for (int y = 0; y < result->num_of_new; y++) {
					*(new_attrs+attr_index) = *(result->attrs+y);
					attr_index++;
				}
			}
			break;
		}
		else if (asn2 == as_path[i]) {
			// stop if asn pair is included already
			if (asn1 == as_path[i+1]) break;
			// extend as_path to add asn1
			num_of_segments++;
			as_path = extend_as_path(as_path, i, num_of_segments, asn1);

			attrs_holding * result = search_for_tail(as_path, i, num_of_segments, routes);
			if (result->num_of_new > 0) {
				for (int y = 0; y < result->num_of_new; y++) {
					*(new_attrs+attr_index) = *(result->attrs+y);
					attr_index++;
				}
			}
			break;
		}
	}
	attrs_holding * new_holding = malloc(sizeof(new_holding));
	new_holding->num_of_new = num_of_new_attrs;
	new_holding->attrs = new_attrs;

	remove_duplicates(new_holding);

	return new_holding;
}

/**
 * Extract the AS_PATH attribute (eattr) from a route.
 *
 * @route: the route that contains the as path
 */
struct eattr * get_as_path_attr(rte * route) {
	struct eattr * as_path_attr;
	if (!(bgp_find_attr(route->attrs->eattrs, BA_AS_PATH))) {
		if (!(bgp_find_attr(route->attrs->eattrs, BA_AS4_PATH))) {
			return NULL;
		} else {
			as_path_attr = bgp_find_attr(route->attrs->eattrs, BA_AS4_PATH);
		}
	} else {
		as_path_attr = bgp_find_attr(route->attrs->eattrs, BA_AS_PATH);
	}
	return as_path_attr;
}


/**
 * Find a neighbor / next_hop struct for a gateway.
 * The gateway is included in the scheduled contact entry.
 * After finding an neighbor, we put the informations in the rta @att structure.
 * This is necessary to set the appropriate next-hop in the routing table.
 *
 * @att: route attributes where we put the informations of the next-hop
 * @p: bgp_proto struct to search for the neighbor
 * @entry: contains informations for the gateway
 */
void add_next_hop(rta * att, struct bgp_proto * p, scheduled_contact_entry * entry) {

	ip_addr * nh = malloc(sizeof(nh));

	if (entry->asn1 == p->public_as) {
		*nh = ipa_from_ip4( entry->gw1  );
	} else if (entry->asn2 == p->public_as ) {
		*nh = ipa_from_ip4( entry->gw2 );
	} else {
		// print error message: no matching asn
	}
	neighbor * neigh = NULL;
	neigh =	neigh_find(&p->p, *nh, NULL, 0);

	if ( !(neigh) ) log(L_INFO "Did not find an interface for IP Address: %x (hex)", nh->addr[3]);

	att->dest = RTD_UNICAST;
	att->nh.gw = neigh->addr;
	att->nh.iface = neigh->iface;
}

/**
 * If we found a new path, we construct a new route that contains this path.
 * We use the old rte as template for some attributes.
 *
 * @rt: pointer to the address of the template-rte (old route)
 * @new_as_path: the new as path attribute
 * @p: bgp_proto struct
 * @entry: the scheduled_contact_entry where the new route originated from
 */
rte * copy_rte_and_insert_as_path(rte ** rt, struct eattr * new_as_path, struct bgp_proto * p, scheduled_contact_entry * entry) {

	rta * old_rta = (*rt)->attrs;

	ea_list * eal_old = (*rt)->attrs->eattrs;
	ea_list * eal_new = NULL;

	while ( !(eal_new) ) {
		eal_new = malloc( sizeof(*eal_old) * (eal_old->count * sizeof(eattr)) );
		memcpy(eal_new, eal_old, sizeof(*eal_old) * (eal_old->count * sizeof(eattr)));

		if ( !(ea_find(eal_new, EA_CODE(PROTOCOL_BGP, BA_AS_PATH))) ) {
			free(eal_new);
			eal_old = eal_old->next;
		}
	}

	eattr * old_attr = ea_find(eal_new, EA_CODE(PROTOCOL_BGP, BA_AS_PATH));
	*(old_attr) = *(new_as_path);

	// only needs new next hop, when the first path segment differs
	_Bool needs_new_nh = 0;

	u32 * new_as = get_as_path(new_as_path);
	eattr * old_path_attr = ea_find(eal_old, EA_CODE(PROTOCOL_BGP, BA_AS_PATH));
	u32 * old_as = get_as_path(old_path_attr);

	if (new_as[0] != old_as[0]) needs_new_nh = 1;

	rta * new_rta = allocz(RTA_MAX_SIZE);

	new_rta->source = RTS_BGP;
	new_rta->scope = SCOPE_UNIVERSE;
	new_rta->from = old_rta->from;
	new_rta->eattrs = eal_new;
	new_rta->dest = RTD_UNICAST;
	new_rta->igp_metric = old_rta->igp_metric;
	new_rta->src = old_rta->src;

	ea_list * neal = new_rta->eattrs;
	rta * crta = rta_lookup(new_rta);
	new_rta->eattrs = neal;
	rta * nrta = rta_clone(crta);
	rte * nrt = rte_get_temp(nrta);
	nrt->pflags = 0;
	nrt->u.bgp.suppressed = 0;
	nrt->u.bgp.stale = -1;

	if (needs_new_nh) {
		add_next_hop( nrta, p, entry );
	} else {
		nrta->nh = old_rta->nh;
	}
	return nrt;
}

/**
 * Print the next-hop of a route.
 *
 * @rt: the route
 *
 * Not used anymore. Only for debugging reasons.
 */
void print_nexthop(rte * rt) {
	rta * att = rt->attrs;
	struct nexthop * nh = &(att->nh);

	log(L_INFO "NEXT HOP: %x %x %x %x", nh->gw.addr[0], nh->gw.addr[1], nh->gw.addr[2], nh->gw.addr[3] );

	if (nh->next) log(L_INFO "NEXT NEXT HOP exists: %x %x %x %x", nh->gw.addr[0], nh->gw.addr[1], nh->gw.addr[2], nh->gw.addr[3]);
}

/**
 * Check if the new route is unique among all others.
 * Check with the path, the destination and the next-hop.
 *
 * @rt: the route that is checked for uniqueness
 * @table: the table in which to search for
 * @ ipv4address: the destination network's IPv4 address
 */
_Bool is_unique_route(rte * rt, rtable * table, u32 * ipv4_address) {

	if (rt == NULL || table == NULL || ipv4_address == NULL) return 0;

	// get AS_PATH
	eattr * new_as_path_attr = get_as_path_attr(rt);
	if (new_as_path_attr == NULL) return 0;
	u8 new_len_as_path = new_as_path_attr->u.ptr->data[1];
	u32 * new_as_path = get_as_path(new_as_path_attr);

	// get Next-Hop
	u32 * new_nh = &rt->attrs->nh.gw.addr[0];

	FIB_WALK(&(table->fib), net, n) {
		rte* oldroute;
		for (oldroute = n->routes; oldroute; oldroute = oldroute->next) {
			// get AS_PATH
			eattr * old_as_path_attr = get_as_path_attr(oldroute);
			if (old_as_path_attr == NULL) continue;
			u8 old_len_as_path = old_as_path_attr->u.ptr->data[1];
			u32 * old_as_path = get_as_path(old_as_path_attr);

			// get Next-Hop
			u32 * old_nh = &rt->attrs->nh.gw.addr[0];
			_Bool same_nh = (new_nh[0] == old_nh[0]) && (new_nh[1] == old_nh[1])
							&& (new_nh[2] == old_nh[2]) && (new_nh[3] == old_nh[3]);

			// get IPv4 address (destination network)
			u8 * old_net_ip = &oldroute->net->n.addr[0].data;
			// cast the u8 pointer to an u32 pointer to get access to the whole 32 bit IPv4 address in the array
			_Bool same_ip = *(ipv4_address) == *((u32 *) old_net_ip);

			if (same_nh && same_ip &&
				check_equal_path(new_as_path, new_len_as_path, old_as_path, old_len_as_path) ) {
				return 0;
			}

		}
	}
	FIB_WALK_END;

	return 1;
}

/*
 * Is called after a scheduled contact begins.
 * Traverses all routes and adds the AS-AS pair from the scheduled contact entry.
 * Here we want to find new routes that becomme possible due to the contact.
 *
 * @ed: entry_data struct that contains various informations needed for this process.
 */
void modify_routingtable_add(entry_data *ed) {

	// get table and sce and check if exists
	struct bgp_proto * proto = ed->proto;
	u32 mypublicasn = proto->public_as;

	struct channel * chl = ed->ch;
	struct rtable *table;
	scheduled_contact_entry * entry = ed->sce;

	if (chl) table = chl->table;
	else return;

	if ( !(table) || !(&(table->fib)) || !(entry) ) return;

	_Bool rt_change = 0;

	FIB_WALK(&(table->fib), net, n) {

		rte* oldroute;

		for (oldroute = n->routes; oldroute; oldroute = oldroute->next) {
			struct eattr * as_path_attr = get_as_path_attr(oldroute);

			if (as_path_attr) {

				attrs_holding * new_as_path_attr = insert_sce_in_path(entry, as_path_attr, n->routes, mypublicasn);

				if (new_as_path_attr) {
					for (int i = 0; i < new_as_path_attr->num_of_new; i++) {
						eattr * tmp_attr = new_as_path_attr->attrs+i;
						rte * new_rte = copy_rte_and_insert_as_path(&oldroute, tmp_attr, proto, entry);

						_Bool unique_route = is_unique_route(new_rte, table, ((u32 *) &n->n.addr[0].data) );

						if (!unique_route) {
							// if the route was not unique, we can delete it
							rte_free(new_rte);
							continue;
						}

						// flags to identify this route in rte_announce
						new_rte->pflags = 0x99;
						rte_update3(chl, &(n->n.addr), new_rte, chl->proto->main_source);
						rt_change = 1;
					}
				}
			}
		}
	}
	FIB_WALK_END;
}


/*
 * Checks if a path contains the AS-AS pair and if so it returns 1,
 * indicating that we should remove this route, when a contact ends.
 *
 * @entry: the scheduled_contact_entry containing the AS-AS pair
 * @as_path_attr: the AS_PATH attribute
 * @mypublicasn: the own ASN
 */

_Bool path_contains_as_pair(scheduled_contact_entry * entry, eattr * as_path_attr, u32 mypublicasn) {
	u32 asn1 = entry->asn1;
	u32 asn2 = entry->asn2;
	u8 num_segments = (as_path_attr->u.ptr->length - 2)/4;
	u32 * path = get_as_path(as_path_attr);

	path = add_first_segment(path, ++num_segments, mypublicasn);

	for (int i = 0; i < num_segments; i++) {
		if ( path[i] == asn1  && path[i+1] == asn2 ) return 1;
		if ( path[i] == asn2  && path[i+1] == asn1 ) return 1;
	}

	return 0;
}

/**
 * Is called after a scheduled contact ends.
 * Traverses all routes and checks which route contains the AS-AS pair from the sce.
 * If a route contains the pair, we add a specific flag and call rte_update3() where the route is deleted.
 *
 * @ed: entry_data struct that contains various informations needed for this process.
 */
void modify_routingtable_remove(entry_data *ed) {
	// get table and sce and chek if exists
	struct bgp_proto * proto = ed->proto;
	u32 mypublicasn = proto->public_as;

	struct channel * chl = ed->ch;
	struct rtable *table;
	scheduled_contact_entry * entry = ed->sce;


	if (chl) table = chl->table;
	else return;

	if ( !(table) || !(&(table->fib)) || !(entry) ) return;

	FIB_WALK(&(table->fib), net, n) {

		rte* oldroute;

		for (oldroute = n->routes; oldroute; oldroute = oldroute->next) {
			struct eattr * as_path_attr = get_as_path_attr(oldroute);

			if (as_path_attr) {
				_Bool routewithdraw = path_contains_as_pair(entry, as_path_attr, mypublicasn);
				// the route contains the AS-AS pair so we remove this route
				if (routewithdraw) {
					// flags to identify this route in rte_announce
					oldroute->pflags = 0x77;
					rte_update3(chl, &(n->n.addr), oldroute, chl->proto->main_source);
				}
			}
		}
	}
	FIB_WALK_END;
}

/*
 * Timer Registration
 */


/**
 * Register timers for every sce in the given entries.
 * One timer is registered for the start_time and the other one, when the
 * contact ends (start_time + duration)
 *
 * @entries: the sces
 * @c: the used channel
 * @proto: the bgp_proto struct
 */
void register_sces(scheduled_contact_entries * entries, struct channel *c, struct bgp_proto * proto) {
	for (int i = 0; i < entries->number_of_entries; i++) {
		scheduled_contact_entry * entry = (entries->entries+i);

		if (entry->start_time == 0 ||
			entry->duration == 0 ||
			entry->asn1 == 0 ||
			entry->gw1 == 0 ||
			entry->asn2 == 0 ||
			entry->gw2 == 0) return;

		u64 begin = entry->start_time;
		u64 end = entry->start_time + entry->duration;
		register_timer(contact_begin, begin, entry, c, proto);
		register_timer(contact_end, end, entry, c, proto);
	}
}

/**
 * Here we register a timer that calls a given function on a given time.
 *
 * @hook: the function that is called
 * @when: the time in milliseconds since 01.01.2000 (UTC) when the timer should fire
 * @sce: the scheduled contact entry
 * @c: the used channel
 * @p: the bgp protocol struct
 *
 */
timer * register_timer(void (*hook)(struct timer *),
		u64 when, scheduled_contact_entry * sce, struct channel *c,
		struct bgp_proto * proto) {

	entry_data * edata = malloc(sizeof(entry_data));

	edata->sce = sce;
	edata->ch = c;
	edata->proto = proto;

	u64 firetime = convert_unixtime_to_secfromnow(when);
	timer * tm = tm_new_init(NULL, hook, edata, 0, 0);

	tm_start(tm, firetime*1000);
	return tm;
}

/**
 * Called by the timer when a contact begins.
 * Invokes the path calculations.
 *
 * @t: the timer that called this method containing entry_data
 */
void
contact_begin(timer *t) {
	entry_data * ed = ((entry_data *) t->data );

	log(L_INFO "\n ==> Begin of contact between AS%u and AS%u !", ed->sce->asn1, ed->sce->asn2);

	if (ed) {
		modify_routingtable_add(ed);
	}
}

/**
 * Called by the timer when a contact ends.
 * Invokes the path calculations.
 *
 * @t: the timer that called this method containing entry_data
 */
void
contact_end(timer *t) {
	entry_data * ed = ((entry_data *) t->data );

	log(L_INFO "\n ==> End of contact between AS%u and AS%u !", ed->sce->asn1, ed->sce->asn2);

	if (ed) {
		modify_routingtable_remove(ed);
	}

	// release resources
	tm_stop(t);
	rfree(t);
}

/**
 *  milliseconds since 01.01.2000 UTC are converted to seconds since now.
 *
 * @relative_time: milliseconds since 01.01.2000 UTC
 */
u64 convert_unixtime_to_secfromnow(u64 relative_time) {
	u64 current_time = time(NULL) * 1000;
	return (relative_time + DTNEPOCH) - current_time;
}


/*
 * Handling, saving, registering scheduled contact entries
 */

/* Stores an sce struct in a file pointed by fd.
 * The struct's raw bytes are stored.
 *
 * @fd: pointer to a file descriptor
 * @entry: the data that needs to be saved
 */
void store_sce(FILE *fd, scheduled_contact_entry *entry) {
	fwrite(entry, sizeof(*entry), 1, fd);
}

/**
 * Stores all sces in a file named SCES_FILENAME.
 *
 * @entries: the scheduled contact entries
 * @c: the used channel
 * @proto: the bgp protocol
 */
void store_sces(scheduled_contact_entries *entries, struct channel *c, struct bgp_proto * proto) {

	// merge existing sces with the new ones to store all together
	scheduled_contact_entries * existing_sces = load_sces();
	scheduled_contact_entries * all_sces;

	// if there are new sces, register timers for them
	if (existing_sces) {
		all_sces = merge_sces(entries, existing_sces);

		// find the new entries that are not in the existing entries and register timers for them
		scheduled_contact_entries * new_entries = find_new_sces(entries, existing_sces);
		register_sces(new_entries, c, proto);
	} else {
		all_sces = entries;

		// if all entries are new (they are because there weren't existing),
		// register new timers for every sce
		register_sces(entries, c, proto);

	}

	FILE *fd = fopen(SCES_FILENAME, "w");

	for (int i = 0; i < all_sces->number_of_entries; i++) {
		scheduled_contact_entry * entry = (all_sces->entries+i);

		if (entry->start_time == 0 ||
			entry->duration == 0 ||
			entry->asn1 == 0 ||
			entry->gw1 == 0 ||
			entry->asn2 == 0 ||
			entry->gw2 == 0) continue;

		store_sce(fd, (all_sces->entries+i));
	}

	fclose(fd);
}

/**
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

	if (num_of_entries < 1) return NULL;

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
 * Finds which sces from the first sces @new are new i.e. not included in the second sces @existing
 * and returns them.
 *
 * @new: the new sces, where new ones are searched.
 * @existing: the existing sces
 */
scheduled_contact_entries * find_new_sces(scheduled_contact_entries * new, scheduled_contact_entries * existing) {

	// first find out how many new sces exists to malloc
	int num_of_new = 0;

	for (int i = 0; i < new->number_of_entries; i++) {
		scheduled_contact_entry * new_e = (new->entries+i);
		_Bool newone = 1;

		// check signature of the entry against all existing signatures
		for (int j = 0; j < existing->number_of_entries; j++) {
			scheduled_contact_entry * existing_e = (existing->entries+j);
			if (check_equal_sces(new_e, existing_e)) newone = 0;
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
		scheduled_contact_entry * new_e = (new->entries+i);
		_Bool newone = 1;

		for (int j = 0; j < existing->number_of_entries; j++) {
			scheduled_contact_entry * existing_e = (existing->entries+j);
			if (check_equal_sces(new_e, existing_e)) newone = 0;
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

/**
 * Checks if two sces are equal, if so it returns 1, otherwise 0;
 *
 * @entry1: the first scheduled contact entry
 * @entry2: the second scheduled contact entry
 */
_Bool check_equal_sces(scheduled_contact_entry * entry1, scheduled_contact_entry * entry2) {
	if (entry1->start_time != entry2->start_time) return 0;
	if (entry1->duration != entry2->duration) return 0;

	if (entry1->asn1 != entry2->asn1 &&
		entry1->asn1 != entry2->asn2) return 0;
	if (entry1->asn2 != entry2->asn1 &&
		entry1->asn2 != entry2->asn2) return 0;

	if (entry1->asn1 == entry2->asn1) {
		if (entry1->asn2 != entry2->asn2) return 0;
	}
	if (entry1->asn1 == entry2->asn2) {
		if (entry1->asn2 != entry2->asn1) return 0;
	}

	if (entry1->gw1 == entry2->gw1) {
		if (entry1->gw2 != entry2->gw2) return 0;
	}
	if (entry1->gw1 == entry2->gw2) {
		if (entry1->gw2 != entry2->gw1) return 0;
	}

	return 1;
}

/**
 * Takes two sets of scheduled contact entries and merges them to one.
 *
 * @entries1: the first set of sces
 * @entries2: the second set of sces
 */
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
		scheduled_contact_entry * e1 = (entries1->entries+i);
		for (int j = 0; j < entries2->number_of_entries; j++) {
			scheduled_contact_entry * e2 = (entries2->entries+j);
			if (check_equal_sces(e1, e2)) duplicates++;
		}
	}
	num_of_entries = entries1->number_of_entries + entries2->number_of_entries - duplicates;
	scheduled_contact_entry * entry_array = malloc(sizeof(scheduled_contact_entry)*num_of_entries);
	/*
	 * first add every entry of entries1
	 * and later add every entry of entries2, but
	 * check if the duplicate entry is inserted
	 * This presupposes that there are no duplicates in entry1
	 */
	int index = 0;
	for (int i = 0; i < entries1->number_of_entries; i++) {
		*(entry_array+i) = *(entries1->entries+i);
		index++;
	}
	for (int i = 0; i < entries2->number_of_entries; i++) {
		// only add the entries, that are not added yet, check added entries from entries1
		_Bool duplicate = 0;
		scheduled_contact_entry * e1 = (entries2->entries+i);
		// compare signature of entry2 against every signature of entry1
		for (int j = 0; j < entries1->number_of_entries; j++) {
			scheduled_contact_entry * e2 = (entries1->entries+j);
			if (check_equal_sces(e1, e2)) duplicate = 1;	// if signature of entry1 & entry2 is identical --> duplicate --> entry2 not added
		}
		if (!duplicate) {
			*(entry_array+index) = *(entries2->entries+i);
			index++;
		}
	}
	entries->number_of_entries = num_of_entries;
	entries->entries = entry_array;

	return entries;
}

/**
 * Prints the informations of all scheduled contact entries
 *
 * @entries: the scheduled contact entries
 */
void print_sces(scheduled_contact_entries *entries) {
	if (!(entries)) return;
	log(L_INFO "===============\nPrinting %u scheduled contact entries.", entries->number_of_entries);
	for (int i = 0; i < entries->number_of_entries; i++) {
		log(L_INFO "Entry %u:\n  =>  %" PRIu64 " %" PRIu64 " %u %u %u %u",
				i+1, (entries->entries+i)->start_time, (entries->entries+i)->duration,
				(entries->entries+i)->asn1, (entries->entries+i)->gw1,
				(entries->entries+i)->asn2, (entries->entries+i)->gw2);
	}
	log(L_INFO "===============\n");
}



/*
 * CBOR En-/Decoding of scheduled contact entries
 */

/**
 * Encode scheduled contact entries to CBOR.
 *
 * @data_size: will contain the size of the data
 */
unsigned char * get_sces_cbor(unsigned int * data_size) {
	scheduled_contact_entries * entries = load_sces();

	if (entries == NULL) return NULL;

	u16 num_of_entries = entries->number_of_entries;

	if (num_of_entries == 0) return NULL;

	// defines upper bound for size of CBOR data object
	// the additional 6 byte are for possible overhead by CBOR,
	// if all fields reach their max. values
	u16 size = num_of_entries * (sizeof(scheduled_contact_entry) + 6);
	unsigned char * buffer = malloc( size );
	unsigned char * data = buffer;

	data = cbor_write_array(data, size, num_of_entries);

	for (u16 i = 0; i < num_of_entries; i++) {
		scheduled_contact_entry * e = (entries->entries+i);

		data = cbor_write_array(data, size, 6);
		data = cbor_write_long(data, size, e->start_time);
		data = cbor_write_long(data, size, e->duration);
		data = cbor_write_int(data, size, e->asn1);
		data = cbor_write_int(data, size, e->gw1);
		data = cbor_write_int(data, size, e->asn2);
		data = cbor_write_int(data, size, e->gw2);
	}

	*data_size = data - buffer;

	return buffer;
}



/*
 * Functions for CBOR support.
 * The following code is made by Stanislav Ovsiannikov
 * from https://github.com/naphaso/cbor-c .
 * The Code is licensed under the Apache License 2.0
 * URL license: https://github.com/naphaso/cbor-c/blob/master/LICENSE
 * The code was modified between the given tags: <modified> ... </modified>.
 */
unsigned int cbor_read_token(unsigned char *data, unsigned int size, unsigned int offset, struct cbor_token *token) {
    if(offset >= size) {
        token->type = CBOR_TOKEN_TYPE_INCOMPLETE;
        return offset;
    }

    unsigned int current_offset = offset;
    unsigned char type = data[current_offset++];
    unsigned char majorType = type >> 5;
    unsigned char minorType = type & 31;
    unsigned int length = 0;

    unsigned int remaining = size - current_offset;

    switch(majorType) {
        case 0: // positive integer
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_INT;
                token->int_value = minorType;
                token->sign = 1;
                return current_offset;
            } else if(minorType == 24) { // 1 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_LONG;
                length = 8;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid positive integer length";
                return offset;
            }
            break;
        case 1: // negative integer
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_INT;
                token->int_value = minorType;
                token->sign = -1;
                return current_offset;
            } else if(minorType == 24) { // 1 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_INT;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_LONG;
                length = 8;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid negative integer length";
                return offset;
            }
            break;
        case 2: // bytes
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_BYTES;
                token->int_value = minorType;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_BYTES;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_BYTES;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_BYTES;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "bytes size too long";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid bytes size";
                return offset;
            }
            break;
        case 3: // string
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_STRING;
                token->int_value = minorType;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_STRING;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_STRING;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_STRING;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "string too long";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid string length";
                return offset;
            }
            break;
        case 4: // array
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                token->int_value = minorType;
                return current_offset;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "array too long";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid array length";
                return offset;
            }
            break;
        case 5: // map
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_MAP;
                token->int_value = minorType;
                return current_offset;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_ARRAY;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "map too long";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid map length";
                return offset;
            }
            break;
        case 6: // tag
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_TAG;
                token->int_value = minorType;
                return current_offset;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_TAG;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_TAG;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_TAG;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "64 bit tags not supported";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid tag";
                return offset;
            }
            break;
        case 7: // special
            if(minorType < 24) {
                token->type = CBOR_TOKEN_TYPE_SPECIAL;
                token->int_value = minorType;
                return current_offset;
            } else if(minorType == 24) {
                token->type = CBOR_TOKEN_TYPE_SPECIAL;
                length = 1;
            } else if(minorType == 25) { // 2 byte
                token->type = CBOR_TOKEN_TYPE_SPECIAL;
                length = 2;
            } else if(minorType == 26) { // 4 byte
                token->type = CBOR_TOKEN_TYPE_SPECIAL;
                length = 4;
            } else if(minorType == 27) { // 8 byte
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "64 bit specials not supported";
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_ERROR;
                token->error_value = "invalid special value";
                return offset;
            }
            break;
        default:
            token->type = CBOR_TOKEN_TYPE_ERROR;
            token->error_value = "unknown error";
            return offset;
    }

    if(length > 0) {
        if(remaining < length) {
            token->type = CBOR_TOKEN_TYPE_INCOMPLETE;
            return offset;
        }

        switch(length) {
            case 1:
                token->int_value = data[current_offset];
                break;
            case 2:
                token->int_value = ((unsigned short)data[current_offset] << 8) | ((unsigned short)data[current_offset + 1]);
                break;
            case 4:
                token->int_value = ((unsigned int)data[current_offset] << 24) | ((unsigned int)data[current_offset + 1] << 16) | ((unsigned int)data[current_offset + 2] << 8) | ((unsigned int)data[current_offset + 3]);
                break;
            case 8:
                token->long_value = ((unsigned long long)data[current_offset] << 56) | ((unsigned long long)data[current_offset +1] << 48) | ((unsigned long long)data[current_offset +2] << 40) | ((unsigned long long)data[current_offset +3] << 32) | ((unsigned long long)data[current_offset +4] << 24) | ((unsigned long long)data[current_offset +5] << 16) | ((unsigned long long)data[current_offset +6] << 8) | ((unsigned long long)data[current_offset +7]);
                if(majorType == 0) {
                    token->sign = 1;
                } else if(majorType == 1) {
                    token->sign = -1;
                }
                return current_offset + 8;
        }

        current_offset += length;
        remaining = size - current_offset;
    }

    switch(majorType) {
        case 0: // positive integer
            token->sign = 1;
            return current_offset;
        case 1: // negative integer
            token->sign = -1;
            return current_offset;
        case 2: // bytes
            if(remaining < token->int_value) {
                token->type = CBOR_TOKEN_TYPE_INCOMPLETE;
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_BYTES;
                token->bytes_value = (data + current_offset);
                return current_offset + token->int_value;
            }
        case 3: // string
            if(remaining < token->int_value) {
                token->type = CBOR_TOKEN_TYPE_INCOMPLETE;
                return offset;
            } else {
                token->type = CBOR_TOKEN_TYPE_STRING;
                token->string_value = (char *) (data + current_offset);
                return current_offset + token->int_value;
            }
        case 4: // array
        case 5: // map
        case 6: // tag
        case 7: // special
            return current_offset;
        default:
            token->type = CBOR_TOKEN_TYPE_ERROR;
            token->error_value = "unknown error";
            return offset;
    }
}


unsigned char *cbor_write_type_size(unsigned char *data, unsigned int size, unsigned int type, unsigned int type_size) {
    type <<= 5;
    if(type_size < 24) {
        if(size < 1) {
            return data;
        }

        *data++ = (unsigned char) (type | type_size);

        return data;
    } else if(type_size < 256) {
        if(size < 2) {
            return data;
        }

        *data++ = (unsigned char) (type | 24);
        *data++ = (unsigned char) type_size;

        return data;
    } else if(type_size < 65536) {
        if(size < 3) {
            return data;
        }

        *data++ = (unsigned char) (type | 25);
        *data++ = (unsigned char) (type_size >> 8);
        *data++ = (unsigned char) type_size;

        return data;
    } else {
        if(size < 5) {
            return data;
        }

        *data++ = (unsigned char) (type | 26);
        *data++ = (unsigned char) (type_size >> 24);
        *data++ = (unsigned char) (type_size >> 16);
        *data++ = (unsigned char) (type_size >> 8);
        *data++ = (unsigned char) type_size;

        return data;
    }
}

unsigned char *cbor_write_type_long_size(unsigned char *data, unsigned int size, unsigned int type, unsigned long long type_size) {
    type <<= 5;
    if(type_size < 24ULL) {
        if(size < 1) {
            return data;
        }

        *data++ = (unsigned char) (type | type_size);

        return data;
    } else if(type_size < 256ULL) {
        if(size < 2) {
            return data;
        }

        *data++     = (unsigned char) (type | 24);
        *data++ = (unsigned char) type_size;

        return data + 2;
    } else if(type_size < 65536ULL) {
        if(size < 3) {
            return data;
        }

        *data++ = (unsigned char) (type | 25);
        *data++ = (unsigned char) (type_size >> 8);
        *data++ = (unsigned char) type_size;

        return data;
    } else if(type_size < 4294967296ULL) {
        if(size < 5) {
            return data;
        }

        *data++ = (unsigned char) (type | 26);
        *data++ = (unsigned char) (type_size >> 24);
        *data++ = (unsigned char) (type_size >> 16);
        *data++ = (unsigned char) (type_size >> 8);
        *data++ = (unsigned char) type_size;

        return data;
    } else {
        if(size < 9) {
            return data;
        }

        *data++ = (unsigned char) (type | 27);
        *data++ = (unsigned char) (type_size >> 56);
        *data++ = (unsigned char) (type_size >> 48);
        *data++ = (unsigned char) (type_size >> 40);
        *data++ = (unsigned char) (type_size >> 32);
        *data++ = (unsigned char) (type_size >> 24);
        *data++ = (unsigned char) (type_size >> 16);
        *data++ = (unsigned char) (type_size >> 8);
        *data++ = (unsigned char) type_size;

        return data;
    }
}

unsigned char *cbor_write_pint(unsigned char *data, unsigned int size, unsigned int value) {
    return cbor_write_type_size(data, size, 0, value);
}

unsigned char *cbor_write_plong(unsigned char *data, unsigned int size, unsigned long long value) {
    return cbor_write_type_long_size(data, size, 0, value);
}

unsigned char *cbor_write_uint(unsigned char *data, unsigned int size, unsigned int value) {
    return cbor_write_pint(data, size, value);
}

unsigned char *cbor_write_ulong(unsigned char *data, unsigned int size, unsigned long long value) {
    return cbor_write_plong(data, size, value);
}
// <modified>
unsigned char *cbor_write_int(unsigned char *data, unsigned int size, int value) {
    return cbor_write_pint(data, size, (unsigned int)(value));
}

unsigned char *cbor_write_long(unsigned char *data, unsigned int size, long long value) {
    return cbor_write_plong(data, size, (unsigned long long)(value));
}
// </modified>
unsigned char *cbor_write_array(unsigned char *data, unsigned int size, unsigned int array_size) {
    return cbor_write_type_size(data, size, 4, array_size);
}


