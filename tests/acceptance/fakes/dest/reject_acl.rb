#!/usr/bin/env ruby

# Accept a connection, then immediately close it.  This simulates an ACL rejection.
# We do not expect a reconnection.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
server.accept.close


begin
  server.accept
  fail "Unexpected reconnection"
rescue Timeout::Error
  # expected
end

server.close


exit(0)
