#!/usr/bin/env ruby

# Will open a server, accept a single connection, then sleep for 5
# seconds.  After that time, the client should have disconnected,
# which we can can't effectively check.
#
# This allows the test runner to check that the command-line sees the
# right error message after the timeout time.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new( addr, port )
client = server.accept( "Client didn't make a connection" )

# Sleep for one second past the timeout (a bit of slop in case ruby
# doesn't launch things quickly)
sleep(FlexNBD::MS_HELLO_TIME_SECS + 1)

client.close
server.close
