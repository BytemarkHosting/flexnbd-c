#!/usr/bin/env ruby
# encoding: utf-8

# Open a server, accept a client, then we expect a single write
# followed by an entrust.  However, we disconnect after the write so
# the entrust will fail.  We expect a reconnection.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, src_pid = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

client.write_hello
req = client.read_request
data = client.read_data( req[:len] )

Process.kill("STOP", src_pid.to_i)
client.write_reply( req[:handle], 0 )
client.close
Process.kill("CONT", src_pid.to_i)

client2 = server.accept
client2.close

exit(0)
