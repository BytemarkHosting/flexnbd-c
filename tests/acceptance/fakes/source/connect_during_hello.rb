#!/usr/bin/env ruby

# Connect to the destination, then hang.  Connect a second time to the
# destination.  This will trigger the destination's thread clearer.

require 'flexnbd/fake_source'
include FlexNBD::FakeSource

addr, port = *ARGV

# client_sock1 is a connection the destination is expecting.
client_sock1 = connect( addr, port, "Timed out connecting" )
sleep(0.25)
client_sock2 = connect( addr, port, "Timed out connecting a second time" )

# This is the expected source crashing after connect
client_sock1.close
# And this is just a tidy-up.
client_sock2.close
