#!/usr/bin/env ruby

# Accept a connection, then immediately close it.  This simulates an ACL rejection.

addr, port = *ARGV
require 'socket'
require 'timeout'

serve_sock = TCPServer.open( addr, port )

begin
  Timeout.timeout( 2 ) do
    serve_sock.accept.close
  end
rescue Timeout::Error
  $stderr.puts "Timed out waiting for a connection"
  exit 1
end

serve_sock.close

exit(0)
