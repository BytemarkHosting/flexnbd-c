#!/usr/bin/env ruby

# Connect, send a migration, entrust then *immediately* disconnect.
# This simulates a client which fails while the client is blocked.
#
# In this situation we expect the destination to quit with an error
# status.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello
client.write_write_request( 0, 8 )
client.write_data( "12345678" )

# Use system "kill" rather than Process.kill because Process.kill
# doesn't seem to work
system "kill -STOP #{srv_pid}"

client.write_entrust_request
client.close

system "kill -CONT #{srv_pid}"


sleep(0.25)

begin
  client2 = FakeSource.new( addr, port, "Expected timeout" )
  fail "Unexpected reconnection"
rescue Timeout::Error
  # expected
end

exit(0)
