#!/usr/bin/env ruby

# Connect, send a migration, entrust then *immediately* disconnect.
# This simulates a client which fails while the client is blocked.
#
# We attempt to reconnect immediately afterwards to prove that we can
# retry the mirroring.

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

client2 = FakeSource.new( addr, port, "Timed out reconnecting" )
client2.close

exit(0)
