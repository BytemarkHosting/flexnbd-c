#!/usr/bin/env ruby
# encoding: utf-8

# Connect, read the hello then make a write request with an impossible
# (from,len) pair.  We expect an error response, and not to be
# disconnected.
#
# We then expect to be able to issue a successful write: the destination
# has to flush the data in the socket.

require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
hello = client.read_hello
client.write_write_request( hello[:size]+1, 32, "myhandle" )
client.write_data("1"*32)
response = client.read_response

fail "Not an error" if response[:error] == 0
fail "Wrong handle" unless "myhandle" == response[:handle]

client.write_write_request( 0, 32 )
client.write_data( "2"*32 )
success_response = client.read_response

fail "Second write failed" unless success_response[:error] == 0

client.close
exit(0)
