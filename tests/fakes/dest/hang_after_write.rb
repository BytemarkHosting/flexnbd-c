#!/usr/bin/env ruby
# encoding: utf-8

# Open a socket, say hello, receive a write, then sleep for >
# MS_REQUEST_LIMIT_SECS seconds. This should tell the source that the
# write has gone MIA, and we expect a reconnect.

require 'flexnbd/fake_dest'
include FlexNBD::FakeDest

sock = serve( *ARGV )
client_sock1 = accept( sock )
write_hello( client_sock1 )
read_request( client_sock1 )

t = Thread.start do
  client_sock2 = accept( sock, "Timed out waiting for a reconnection",
                        FlexNBD::MS_REQUEST_LIMIT_SECS + 2 )
  client_sock2.close
end

sleep( FlexNBD::MS_REQUEST_LIMIT_SECS + 2 )
client_sock1.close

t.join

exit(0)
