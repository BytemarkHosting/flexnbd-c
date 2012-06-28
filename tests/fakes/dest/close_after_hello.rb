#!/usr/bin/env ruby

# Wait for a sender connection, send a correct hello, then disconnect.
# Simulate a server which crashes after sending the hello.  We then
# reopen the server socket to check that the sender retries: since the
# command-line has gone away, and can't feed an error back to the
# user, we have to keep trying.

require 'flexnbd/fake_dest'
include FlexNBD::FakeDest

addr, port = *ARGV


sock = serve( addr, port )
client_sock = accept( sock, "Timed out waiting for a connection" )
write_hello( client_sock )
client_sock.close

new_sock = accept( sock, "Timed out waiting for a reconnection" )

new_sock.close
sock.close

exit 0
