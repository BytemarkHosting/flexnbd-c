#!/usr/bin/env ruby

# Connects to the destination server, then immediately disconnects,
# simulating a source crash.
#
# It then connects again, to check that the destination is still
# listening.

require 'flexnbd/fake_source'
include FlexNBD

addr, port = *ARGV

FakeSource.new( addr, port, "Failed to connect" ).close
  # Sleep to be sure we don't try to connect too soon. That wouldn't
  # be a problem for the destination, but it would prevent us from
  # determining success or failure here in the case where we try to
  # reconnect before the destination has tidied up after the first
  # thread went away.
sleep(0.5)
FakeSource.new( addr, port, "Failed to reconnect" ).close

exit 0
