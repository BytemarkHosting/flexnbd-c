#!/usr/bin/env ruby
# encoding: utf-8

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

client.write_hello
handle = client.read_request[:handle]
client.write_error( handle )


client2 = server.accept( "Timed out waiting for a reconnection" )

client.close
client2.close
server.close

exit(0)
