#!/usr/bin/env ruby

# Connect, send a migration, entrust, read the reply, then disconnect.
# This simulates a client which fails while the client is blocked.
#
# We expect the destination to quit with an error status.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello
client.write_write_request( 0, 8 )
client.write_data( "12345678" )

client.write_entrust_request
client.read_response
client.close

sleep(0.25)


begin
  client2 = FakeSource.new( addr, port, "Expected timeout" )
  fail "Unexpected reconnection"
rescue Timeout::Error
  # expected
end

exit(0)

