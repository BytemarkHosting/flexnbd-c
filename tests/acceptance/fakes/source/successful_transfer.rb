#!/usr/bin/env ruby

# Successfully send a migration.  This test just makes sure that the
# happy path is covered.  We expect the destination to quit with a
# success status.

require 'flexnbd/fake_source'
include FlexNBD

addr, port, srv_pid, newaddr, newport = *ARGV

client = FakeSource.new(addr, port, 'Timed out connecting')
client.send_mirror

sleep(1)

exit(0)
