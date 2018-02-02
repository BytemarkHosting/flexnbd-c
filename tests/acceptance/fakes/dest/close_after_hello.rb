#!/usr/bin/env ruby

# Wait for a sender connection, send a correct hello, then disconnect.
# Simulate a server which crashes after sending the hello.  We then
# reopen the server socket to check that the sender retries: since the
# command-line has gone away, and can't feed an error back to the
# user, we have to keep trying.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new(addr, port)
client = server.accept('Timed out waiting for a connection')
client.write_hello
client.close

new_client = server.accept('Timed out waiting for a reconnection')
new_client.close

server.close

exit 0
