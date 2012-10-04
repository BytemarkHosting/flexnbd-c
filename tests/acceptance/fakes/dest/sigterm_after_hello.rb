#!/usr/bin/env ruby

# Wait for a sender connection, send a correct hello, then sigterm the
# sender.  We expect the sender to exit with status of 6, which is
# enforced in the test.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, pid = *ARGV
server = FakeDest.new( addr, port )
client = server.accept( "Timed out waiting for a connection" )
client.write_hello

Process.kill(15, pid.to_i)

client.close
server.close
exit 0
