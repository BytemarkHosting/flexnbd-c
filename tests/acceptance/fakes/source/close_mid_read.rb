#!/usr/bin/env ruby

# Connect, but get the protocol wrong: don't read the hello, so we
# close and break the sendfile.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid, newaddr, newport = *ARGV

client = FakeSource.new( addr, port, "Timed out connecting" )
client.write_read_request( 0, 8 )
client.read_raw( 4 )
client.close


exit(0)
