#!/usr/bin/env ruby
# encoding: utf-8

# Accept a connection, write hello, wait for a write request, read the
# data, then write back a reply with a bad magic field.  We then
# expect a reconnect.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )

client = server.accept
client.write_hello
req = client.read_request
client.read_data( req[:len] )
client.write_reply( req[:handle], 0, :magic => :wrong )

client2 = server.accept
client.close
client2.close

exit(0)
