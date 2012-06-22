#!/usr/bin/env ruby

# Simulate the hello message going astray, or the source hanging after
# receiving it.
#
# We then connect again, to confirm that the destination is still
# listening for an incoming migration.

addr, port = *ARGV
require 'socket'
require 'timeout'

require "flexnbd/constants"


client_sock=nil
begin
  Timeout.timeout(2) do
    client_sock = TCPSocket.open( addr, port )
  end
rescue Timeout::Error
  $stderr.puts "Timed out connecting"
  exit 1
end

begin
  Timeout.timeout(FlexNBD::MS_HELLO_TIME_SECS) do
    client_sock.read(152)
  end
rescue Timeout::Error
  $stderr.puts "Timed out reading hello"
  exit 1
end

# Now we do two things:

# - In the parent process, we sleep for CLIENT_MAX_WAIT_SECS+5, which
#   will make the destination give up and close the connection.
# - In the child process, we sleep for CLIENT_MAX_WAIT_SECS+1, which
#   should be able to reconnect despite the parent process not having
#   closed its end yet.

kidpid = fork do
  client_sock.close
  new_sock = nil
  sleep( FlexNBD::CLIENT_MAX_WAIT_SECS + 1 )
  begin
    Timeout.timeout( 2 ) do
      new_sock = TCPSocket.open( addr, port )
    end
    Timeout.timeout( FlexNBD::MS_HELLO_TIME_SECS ) do
      fail "No hello." unless (hello = new_sock.read( 152 )) &&
          hello.length==152
    end
    new_sock.close
  rescue Timeout::Error
    $stderr.puts "Timed out reconnecting"
    exit 1
  end
  exit 0
end

# Sleep for longer than the child, to give the flexnbd process a bit
# of slop
sleep( FlexNBD::CLIENT_MAX_WAIT_SECS + 3 )
client_sock.close

_,status = Process.waitpid2( kidpid )
exit status.exitstatus
