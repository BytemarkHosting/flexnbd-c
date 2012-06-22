#!/usr/bin/env ruby

# Connects to the destination server, then immediately disconnects,
# simulating a source crash.
#
# It then connects again, to check that the destination is still
# listening.

addr, port = *ARGV
require 'socket'
require 'timeout'

begin
  Timeout.timeout( 2 ) do
    sock = TCPSocket.open( addr, port.to_i )
    sock.close
  end
rescue Timeout::Error
  $stderr.puts "Failed to connect"
  exit 1
end

Timeout.timeout( 2 ) do
  sock = TCPSocket.open( addr, port.to_i )
  sock.close
end

exit 0
