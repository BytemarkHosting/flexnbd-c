#ifndef ACL_H
#define ACL_H

#include "parse.h"

struct acl {
    int len;
    int default_deny;
    struct ip_and_mask (*entries)[];
};

/** Allocate a new acl structure, parsing the given lines to sockaddr
 * structures in the process.  After allocation, acl->len might not
 * equal len.  In that case, there was an error in parsing and acl->len
 * will be the index of the failed entry in lines.
 *
 * default_deny controls the behaviour of an empty list: if true, all
 * requests will be denied.  If true, all requests will be accepted.
 */
struct acl *acl_create(int len, char **lines, int default_deny);


/** Check to see whether an address is allowed by an acl.
 * See acl_create for how the default_deny setting affects this.
 */
int acl_includes(struct acl *, union mysockaddr *);

/** Get the default_deny status */
int acl_default_deny(struct acl *);


/** Free the acl structure and the internal acl entries table.
 */
void acl_destroy(struct acl *);


#endif
