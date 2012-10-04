#!/usr/bin/env ruby

# Connect to the listener, wait for the hello, then sigterm the
# listener.  We expect the listener to exit with a status of 6, which
# is enforced in the test.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, pid = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting." )
client.read_hello

Process.kill( "TERM", pid.to_i )

sleep(0.2)
client.close

exit(0)
