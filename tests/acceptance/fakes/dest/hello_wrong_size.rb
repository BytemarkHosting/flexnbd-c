#!/usr/bin/env ruby

# Simulate a server which has a disc of the wrong size attached: send
# a valid NBD hello with a random size, then check that we have see an
# EOF on read.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

t = Thread.new do
  client2 = server.accept( "Timed out waiting for a reconnection",
                    FlexNBD::MS_RETRY_DELAY_SECS + 1 )
  client2.close
end

client.write_hello( :size => :wrong )

t.join

exit 0
