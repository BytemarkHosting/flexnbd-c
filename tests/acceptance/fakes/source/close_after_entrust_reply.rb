#!/usr/bin/env ruby

# Connect, send a migration, entrust then *immediately* disconnect.
# This simulates a client which fails while the client is blocked.
#
# We attempt to reconnect immediately afterwards to prove that we can
# retry the mirroring.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid, rebind_addr, rebind_port = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello
client.write_write_request( 0, 8 )
client.write_data( "12345678" )

client.write_entrust_request
client.read_response
client.close

sleep(0.25)

client2 = FakeSource.new( addr, port, "Timed out reconnecting to mirror" )
client2.send_mirror

sleep(1)
client3 = FakeSource.new( rebind_addr, rebind_port, "Timed out reconnecting to read" )
client3.close

exit(0)
