#!/usr/bin/env ruby

# Simulate a server which has a disc of the wrong size attached: send
# a valid NBD hello with a random size, then check that we have see an
# EOF on read.

require 'flexnbd/fake_dest'
include FlexNBD::FakeDest

sock = serve( *ARGV )
client_sock = accept( sock, "Timed out waiting for a connection" )


t = Thread.new do
  client_sock2 = accept( sock, "Timed out waiting for a reconnection",
                         FlexNBD::MS_RETRY_DELAY_SECS + 1 )
  client_sock2.close
end

write_hello( client_sock, :size => :wrong )

t.join

exit 0
