#!/usr/bin/env ruby
# encoding: utf-8

# We connect from a local address which should be blocked, sleep for a
# bit, then try to read from the socket. We should get an instant EOF
# as we've been cut off by the destination.

require 'timeout'
require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting", "127.0.0.6" )
sleep( 0.25 )

rsp = client.disconnected? ? 0 : 1
client.close
exit(rsp)

