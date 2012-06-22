#!/usr/bin/env ruby

# Connect, read the hello, then immediately disconnect.  This
# simulates a sender which dislikes something in the hello message - a
# wrong size, for instance.

# After the disconnect, we reconnect to be sure that the destination
# is still alive.


require 'socket'
require "timeout"
require 'flexnbd/constants'

addr, port = *ARGV

client_sock = nil
begin
  Timeout.timeout(2) do
    client_sock = TCPSocket.open( addr, port )
  end
rescue Timeout::Error
  $stderr.puts "Timed out connecting."
  exit 1
end

begin
  Timeout.timeout( FlexNBD::MS_HELLO_TIME_SECS ) do
    fail "No hello." unless (hello = client_sock.read( 152 )) &&
      hello.length==152
  end
rescue Timeout::Error
  $stderr.puts "Timed out waiting for hello."
  exit 1
end

client_sock.close

sleep(0.2)
begin
  Timeout.timeout(2) do
    client_sock = TCPSocket.open( addr, port )
  end
rescue Timeout::Error
  $stderr.puts "Timed out reconnecting."
  exit 1
end

exit(0)
