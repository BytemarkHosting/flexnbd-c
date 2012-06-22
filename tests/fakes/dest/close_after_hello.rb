#!/usr/bin/env ruby

# Wait for a sender connection, send a correct hello, then disconnect.
# Simulate a server which crashes after sending the hello.  We then
# reopen the server socket to check that the sender retries: since the
# command-line has gone away, and can't feed an error back to the
# user, we have to keep trying.

addr, port = *ARGV

require 'socket'
require 'timeout'

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


client_sock.write( "NBDMAGIC" )
client_sock.write( "\x00\x00\x42\x02\x81\x86\x12\x53" )
client_sock.write( "\x00\x00\x00\x00\x00\x00\x10\x00" )
client_sock.write( "\x00" * 128 )

client_sock.close

new_sock = nil
begin
  Timeout.timeout(60) do
    new_sock = sock.accept
  end
rescue Timeout::Error
  $stderr.puts "Timed out waiting for a reconnection"
  exit 1
end

new_sock.close
sock.close

exit 0
