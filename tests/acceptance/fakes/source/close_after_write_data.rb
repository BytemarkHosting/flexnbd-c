#!/usr/bin/env ruby
# encoding: utf-8

# We connect, pause the server, issue a write request, send data,
# disconnect, then cont the server. This ensures that our disconnect
# happens before the server can try to write the reply.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello

Process.kill( "STOP", srv_pid.to_i )

client.write_write_request( 0, 8 )
client.write_data( "12345678" )
client.close

Process.kill( "CONT", srv_pid.to_i )

# This sleep ensures that we don't return control to the test runner
# too soon, giving the flexnbd process time to fall over if it's going
# to.
sleep(0.25)

# ...and can we reconnect?
client2 = FakeSource.new( addr, port, "Timed out reconnecting" )
client2.close

exit(0)
