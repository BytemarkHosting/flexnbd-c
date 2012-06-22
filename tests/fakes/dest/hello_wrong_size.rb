#!/usr/bin/env ruby

# Simulate a server which has a disc of the wrong size attached: send
# a valid NBD hello with a random size, then check that we have see an
# EOF on read.

addr, port = *ARGV

require 'socket'
require 'timeout'
require 'flexnbd/constants'

sock = TCPServer.new( addr, port )
client_sock = nil

begin
  Timeout.timeout(2) do
    client_sock = sock.accept
  end
rescue Timeout::Error
  $stderr.puts "Timed out waiting for a connection"
  exit 1
end

t = Thread.new do
  begin
    Timeout.timeout(FlexNBD::MS_RETRY_DELAY_SECS + 1) do
      client_sock2 = sock.accept
    end
  rescue Timeout::Error
    $stderr.puts "Timed out waiting for a reconnection"
    exit 1
  end
end

client_sock.write( "NBDMAGIC" )
client_sock.write( "\x00\x00\x42\x02\x81\x86\x12\x53" )
8.times do client_sock.write rand(256).chr end
client_sock.write( "\x00" * 128 )


t.join

exit 0
