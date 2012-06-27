#!/usr/bin/env ruby

# Connect, read the hello, then immediately disconnect.  This
# simulates a sender which dislikes something in the hello message - a
# wrong size, for instance.

# After the disconnect, we reconnect to be sure that the destination
# is still alive.

require 'flexnbd/fake_source'
include FlexNBD::FakeSource

addr, port = *ARGV


client_sock = connect( addr, port, "Timed out connecting." )
read_hello( client_sock )
client_sock.close
sleep(0.2)
connect( addr, port, "Timed out reconnecting." )

exit(0)
