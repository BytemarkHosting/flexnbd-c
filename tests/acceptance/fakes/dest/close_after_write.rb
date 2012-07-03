#!/usr/bin/env ruby

# Wait for a sender connection, send a correct hello, wait for a write
# request, then disconnect.  Simulate a server which crashes after
# receiving the write, and before it can send a reply.  We then reopen
# the server socket to check that the sender retries: since the
# command-line has gone away, and can't feed an error back to the
# user, we have to keep trying.

require 'flexnbd/fake_dest'
include FlexNBD

server = FakeDest.new( *ARGV )
client = server.accept( "Timed out waiting for a connection" )
client.write_hello
client.read_request
client.close

new_client = server.accept( "Timed out waiting for a reconnection" )
new_client.close

server.close

exit 0
