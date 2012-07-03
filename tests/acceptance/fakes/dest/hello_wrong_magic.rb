#!/usr/bin/env ruby

# Simulate a destination which sends the wrong magic.
# We expect the sender to disconnect and reconnect.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client1 = server.accept

# Launch a second thread so that we can spot the reconnection attempt
# as soon as it happens, or alternatively die a flaming death on
# timeout.
t = Thread.new do
  client2 = server.accept( "Timed out waiting for a reconnection",
                           FlexNBD::MS_RETRY_DELAY_SECS + 1 )
  client2.close
end

client1.write_hello( :magic => :wrong )

t.join

exit 0
