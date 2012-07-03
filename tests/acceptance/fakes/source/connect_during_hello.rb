#!/usr/bin/env ruby

# Connect to the destination, then hang.  Connect a second time to the
# destination.  This will trigger the destination's thread clearer. We
# can't really see any error state from here, we just try to trigger
# something the test runner can detect.

require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV

client1 = FakeSource.new( addr, port, "Timed out connecting" )
sleep(0.25)
client2 = FakeSource.new( addr, port, "Timed out connecting a second time" )

# This is the expected source crashing after connect
client1.close
# And this is just a tidy-up.
client2.close

exit(0)
