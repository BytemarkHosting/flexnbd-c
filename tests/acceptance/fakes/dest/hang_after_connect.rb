#!/usr/bin/env ruby

# Will open a server, accept a single connection, then sleep for 5
# seconds.  After that time, the client should have disconnected,
# which we can can't effectively check.
#
# We also expect the client *not* to reconnect, since it could feed back
# an error.
#
# This allows the test runner to check that the command-line sees the
# right error message after the timeout time.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new(addr, port)
client = server.accept("Client didn't make a connection")

# Sleep for one second past the timeout (a bit of slop in case ruby
# doesn't launch things quickly)
sleep(FlexNBD::MS_HELLO_TIME_SECS + 1)

client.close

# Invert the sense of the timeout exception, since we *don't* want a
# connection attempt
begin
  server.accept('Expected timeout')
  raise 'Unexpected reconnection'
rescue Timeout::Error
  # expected
end

server.close
