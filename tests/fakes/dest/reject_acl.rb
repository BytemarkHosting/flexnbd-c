#!/usr/bin/env ruby

# Accept a connection, then immediately close it.  This simulates an ACL rejection.

require 'flexnbd/fake_dest'
include FlexNBD

server = FakeDest.new( *ARGV )
server.accept.close

server.close

exit(0)
