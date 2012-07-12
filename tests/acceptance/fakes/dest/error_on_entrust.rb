#!/usr/bin/env ruby
# encoding: utf-8

# Receive a mirror, but respond to the entrust with an error.  There's
# currently no code path in flexnbd which can do this, but we could
# add one.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

client.write_hello
loop do
  req = client.read_request
  if req[:type] == 1
    client.read_data( req[:len] )
    client.write_reply( req[:handle] )
  else
    client.write_reply( req[:handle], 1 )
    break
  end
end

client.close

client2 = server.accept( "Timed out waiting for a reconnection" )

client2.close
server.close

exit(0)
