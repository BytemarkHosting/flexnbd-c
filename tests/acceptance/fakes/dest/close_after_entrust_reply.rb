#!/usr/bin/env ruby
# encoding: utf-8

# Receive a mirror, and disconnect after sending the entrust reply but
# before it can send the disconnect signal.
#
# This test is currently unused: the sender can't detect that the
# write failed.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, src_pid = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

client.write_hello
while (req = client.read_request; req[:type] == 1)
  client.read_data( req[:len] )
  client.write_reply( req[:handle] )
end

system "kill -STOP #{src_pid}"
client.write_reply( req[:handle] )
client.close
system "kill -CONT #{src_pid}"

sleep( 0.25 )
client2 = server.accept( "Timed out waiting for a reconnection" )

client2.close
server.close

$stderr.puts "done"
exit(0)

