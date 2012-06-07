#include <stdlib.h>

#include "util.h"
#include "parse.h"

#include "acl.h"


struct acl * acl_create( int len, char ** lines, int default_deny )
{
	struct acl * acl;

	acl = (struct acl *)xmalloc( sizeof( struct acl ) );
	acl->len = parse_acl( &acl->entries, len, lines );
	acl->default_deny = default_deny;
	return acl;
}


static int testmasks[9] = { 0,128,192,224,240,248,252,254,255 };

/** Test whether AF_INET or AF_INET6 sockaddr is included in the given access
  * control list, returning 1 if it is, and 0 if not.
  */
static int is_included_in_acl(int list_length, struct ip_and_mask (*list)[], union mysockaddr* test)
{
	NULLCHECK( test );

	int i;
	
	for (i=0; i < list_length; i++) {
		struct ip_and_mask *entry = &(*list)[i];
		int testbits;
		unsigned char *raw_address1, *raw_address2;
		
		debug("checking acl entry %d (%d/%d)", i, test->generic.sa_family, entry->ip.family);
		
		if (test->generic.sa_family != entry->ip.family) {
			continue;
		}
		
		if (test->generic.sa_family == AF_INET) {
			debug("it's an AF_INET");
			raw_address1 = (unsigned char*) &test->v4.sin_addr;
			raw_address2 = (unsigned char*) &entry->ip.v4.sin_addr;
		}
		else if (test->generic.sa_family == AF_INET6) {
			debug("it's an AF_INET6");
			raw_address1 = (unsigned char*) &test->v6.sin6_addr;
			raw_address2 = (unsigned char*) &entry->ip.v6.sin6_addr;
		}
		
		debug("testbits=%d", entry->mask);
		
		for (testbits = entry->mask; testbits > 0; testbits -= 8) {
			debug("testbits=%d, c1=%02x, c2=%02x", testbits, raw_address1[0], raw_address2[0]);
			if (testbits >= 8) {
				if (raw_address1[0] != raw_address2[0])
					goto no_match;
			}
			else {
				if ((raw_address1[0] & testmasks[testbits%8]) !=
				    (raw_address2[0] & testmasks[testbits%8]) )
				    	goto no_match;
			}
			
			raw_address1++;
			raw_address2++;
		}
		
		return 1;
		
		no_match: ;
		debug("no match");
	}
	
	return 0;
}

int acl_includes( struct acl * acl, union mysockaddr * addr )
{
	NULLCHECK( acl );

	if ( 0 == acl->len ) {
		return !( acl->default_deny );
	} 
	else {
		return is_included_in_acl( acl->len, acl->entries, addr );
	}
}


void acl_destroy( struct acl * acl )
{
	free( acl->entries );
	acl->len = 0;
	acl->entries = NULL;
	free( acl );
}
