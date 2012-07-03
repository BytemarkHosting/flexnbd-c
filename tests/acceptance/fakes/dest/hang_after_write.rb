#!/usr/bin/env ruby
# encoding: utf-8

# Open a socket, say hello, receive a write, then sleep for >
# MS_REQUEST_LIMIT_SECS seconds. This should tell the source that the
# write has gone MIA, and we expect a reconnect.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client1 = server.accept( server )
client1.write_hello
client1.read_request

t = Thread.start do
  client2 = server.accept( "Timed out waiting for a reconnection",
                        FlexNBD::MS_REQUEST_LIMIT_SECS + 2 )
  client2.close
end

sleep( FlexNBD::MS_REQUEST_LIMIT_SECS + 2 )
client1.close

t.join

server.close
exit(0)
