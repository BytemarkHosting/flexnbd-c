#include "prefetch.h"
#include "util.h"


struct prefetch* prefetch_create( size_t size_bytes ){

	struct prefetch* out = xmalloc( sizeof( struct prefetch ) );
	NULLCHECK( out );
	
	out->buffer = xmalloc( size_bytes );
	NULLCHECK( out->buffer );

	out->size = size_bytes;
	out->is_full = 0;
	out->from = 0;
	out->len = 0;

	return out;

}

void prefetch_destroy( struct prefetch *prefetch ) {
	if( prefetch ) {
		free( prefetch->buffer );
		free( prefetch );
	}
}

size_t prefetch_size( struct prefetch *prefetch){
	if ( prefetch ) {
		return prefetch->size;
	} else {
		return 0;
	}
}

void prefetch_set_is_empty( struct prefetch *prefetch ){
	prefetch_set_full( prefetch, 0 );
}

void prefetch_set_is_full( struct prefetch *prefetch ){
	prefetch_set_full( prefetch, 1 );
}

void prefetch_set_full( struct prefetch *prefetch, int val ){
	if( prefetch ) {
		prefetch->is_full = val;
	}
}

int prefetch_is_full( struct prefetch *prefetch ){
	if( prefetch ) {
		return prefetch->is_full;
	} else {
		return 0;
	}
}

int prefetch_contains( struct prefetch *prefetch, uint64_t from, uint32_t len ){
	NULLCHECK( prefetch );
	return from >= prefetch->from &&
		from + len <= prefetch->from + prefetch->len;
}

char *prefetch_offset( struct prefetch *prefetch, uint64_t from ){
	NULLCHECK( prefetch );
	return prefetch->buffer + (from - prefetch->from);
}
