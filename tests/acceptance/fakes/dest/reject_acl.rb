#!/usr/bin/env ruby

# Accept a connection, then immediately close it.  This simulates an ACL rejection.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
server.accept.close

server.close

exit(0)
