#!/usr/bin/env ruby

# Successfully send a migration, but squat on the IP and port which
# the destination wants to rebind to.  The destination should retry
# every second, so we give it up then attempt to connect to the new
# server.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid, newaddr, newport = *ARGV

squatter = TCPServer.open( newaddr, newport.to_i )

client  = FakeSource.new( addr, port, "Timed out connecting" )
client.send_mirror()

sleep(1)

squatter.close()

sleep(1)

client2 = FakeSource.new( newaddr, newport.to_i, "Timed out reconnecting" )
client2.read_hello
client2.read( 0, 8 )
client2.close

exit( 0 )
