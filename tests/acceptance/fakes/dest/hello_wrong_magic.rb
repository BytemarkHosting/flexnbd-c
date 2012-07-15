#!/usr/bin/env ruby

# Simulate a destination which sends the wrong magic.

require 'flexnbd/fake_dest'
include FlexNBD
Thread.abort_on_exception

addr, port = *ARGV
server = FakeDest.new( addr, port )
client1 = server.accept

# We don't expect a reconnection attempt.
t = Thread.new do
  begin
    client2 = server.accept( "Timed out waiting for a reconnection",
                            FlexNBD::MS_RETRY_DELAY_SECS + 1 )
    fail "Unexpected reconnection"
  rescue Timeout::Error
    #expected
  end
end

client1.write_hello( :magic => :wrong )

t.join

exit 0
