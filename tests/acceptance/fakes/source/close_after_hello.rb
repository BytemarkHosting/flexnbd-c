#!/usr/bin/env ruby

# Connect, read the hello, then immediately disconnect.  This
# simulates a sender which dislikes something in the hello message - a
# wrong size, for instance.

# After the disconnect, we reconnect to be sure that the destination
# is still alive.

require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV


client = FakeSource.new( addr, port, "Timed out connecting." )
client.read_hello
client.close

sleep(0.2)

FakeSource.new( addr, port, "Timed out reconnecting." )

exit(0)
