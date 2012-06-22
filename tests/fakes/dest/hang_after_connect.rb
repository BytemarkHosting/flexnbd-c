#!/usr/bin/env ruby

# Will open a server, accept a single connection, then sleep for 5
# seconds.  After that time, the client should have disconnected,
# which we can can't effectively check.
#
# This allows the test runner to check that the command-line sees the
# right error message after the timeout time.

require 'socket'
require 'timeout'
require 'flexnbd/constants'

addr, port = *ARGV

serve_sock = TCPServer.new( addr, port )
client_sock = nil

begin
  # A failure here means a more serious issue with flexnbd
  Timeout.timeout( 2 ) do
    client_sock = serve_sock.accept
  end
rescue Timeout::Error
  $stderr.puts "Client didn't make connection"
  exit 1
end

# Sleep for one second past the timeout (a bit of slop in case ruby
# doesn't launch things quickly)
sleep(FlexNBD::MS_HELLO_TIME_SECS + 1)

client_sock.close  if client_sock
serve_sock.close
