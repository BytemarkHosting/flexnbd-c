#!/usr/bin/env ruby

# Simulate the hello message going astray, or the source hanging after
# receiving it.
#
# We then connect again, to confirm that the destination is still
# listening for an incoming migration.

addr, port = *ARGV
require "flexnbd/fake_source"
include FlexNBD

client = FakeSource.new( addr, port, "Timed out connecting" )
client.read_hello

# Now we do two things:

# - In the parent process, we sleep for CLIENT_MAX_WAIT_SECS+5, which
#   will make the destination give up and close the connection.
# - In the child process, we sleep for CLIENT_MAX_WAIT_SECS+1, which
#   should be able to reconnect despite the parent process not having
#   closed its end yet.

kidpid = fork do
  client.close
  new_client = nil
  sleep( FlexNBD::CLIENT_MAX_WAIT_SECS + 1 )
  new_client = FakeSource.new( addr, port, "Timed out reconnecting." )
  new_client.read_hello
  exit 0
end

# Sleep for longer than the child, to give the flexnbd process a bit
# of slop
sleep( FlexNBD::CLIENT_MAX_WAIT_SECS + 3 )
client.close

_,status = Process.waitpid2( kidpid )
exit status.exitstatus
