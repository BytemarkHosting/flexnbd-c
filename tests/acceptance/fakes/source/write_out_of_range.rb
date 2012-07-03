#!/usr/bin/env ruby
# encoding: utf-8

# Connect, read the hello then make a write request with an impossible
# (from,len) pair.  We expect an error response, and not to be
# disconnected.

require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello
client.write_write_request( 1 << 31, 1 << 31, "myhandle" )
response = client.read_response

fail "Not an error" if response[:error] == 0
fail "Wrong handle" unless "myhandle" == response[:handle]

client.close
exit(0)
