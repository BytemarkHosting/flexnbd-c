#!/usr/bin/env ruby

# Simulate a destination which sends the wrong magic.
# We expect the sender to disconnect and reconnect.

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
# We're off in the last byte, should be \x53
client_sock.write( "\x00\x00\x42\x02\x81\x86\x12\x52" )
# 4096 is the right size; this is defined in nbd_scenarios
client_sock.write( "\x00\x00\x00\x00\x00\x00\x10\x00" )
client_sock.write( "\x00" * 128 )


t.join

exit 0
