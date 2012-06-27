#!/usr/bin/env ruby

# Connect to the destination, then hang.  Connect a second time to the
# destinatioin.  This will trigger the destination's thread clearer.

require 'socket'
require 'timeout'

addr, port = *ARGV

# client_sock1 is a connection the destination is expecting.
client_sock1 = nil
begin
  Timeout.timeout(2) do
    client_sock1 = TCPSocket.open( addr, port )
  end
rescue Timeout::Error
  $stderr.puts "Timed out connecting."
  exit 1
end

#client_sock2 is the interloper.
client_sock2 = nil
begin
  Timeout.timeout(2) do
    client_sock2 = TCPSocket.open( addr, port )
  end
rescue Timeout::Error
  $stderr.puts "Timed out connecting a second time."
  exit 1
end

# This is the expected source crashing after connect
client_sock1.close
# And this is just a tidy-up.
client_sock2.close
