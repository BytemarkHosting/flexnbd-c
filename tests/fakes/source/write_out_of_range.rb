#!/usr/bin/env ruby
# encoding: utf-8

# Connect, read the hello then make a write request with an impossible
# (from,len) pair.  We expect an error response, and not to be
# disconnected.

require 'flexnbd/fake_source'
include FlexNBD::FakeSource

addr, port = *ARGV
client_sock = connect( addr, port, "Timed out connecting" )
read_hello( client_sock )
write_write_request( client_sock, 1 << 31, 1 << 31, "myhandle" )
response = read_response( client_sock )

fail "Not an error" if response[:error] == 0
fail "Wrong handle" unless "myhandle" == response[:handle]

client_sock.close
exit(0)
