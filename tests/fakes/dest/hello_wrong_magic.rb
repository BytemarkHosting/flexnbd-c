#!/usr/bin/env ruby

# Simulate a destination which sends the wrong magic.
# We expect the sender to disconnect and reconnect.

require 'flexnbd/fake_dest'
include FlexNBD::FakeDest

sock = serve( *ARGV )
client_sock = accept( sock, "Timed out waiting for a connection" )

# Launch a second thread so that we can spot the reconnection attempt
# as soon as it happens, or alternatively die a flaming death on
# timeout.
t = Thread.new do
  client_sock2 = accept( sock, "Timed out waiting for a reconnection",
                         FlexNBD::MS_RETRY_DELAY_SECS + 1 )
  client_sock2.close
end

write_hello( client_sock, :magic => :wrong )

t.join

exit 0
