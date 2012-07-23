#!/usr/bin/env ruby
# encoding: utf-8

# Open a server, accept a client, then cancel the migration by issuing
# a break command.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, src_pid, sock = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

ctrl = UNIXSocket.open( sock )

Process.kill("STOP", src_pid.to_i)
ctrl.write( "break\n" )
ctrl.close_write
client.write_hello
Process.kill("CONT", src_pid.to_i)

fail "Unexpected control response" unless
  ctrl.read =~ /0: mirror stopped/

client2 = nil
begin
  client2 = server.accept( "Expected timeout" )
  fail "Unexpected reconnection"
rescue Timeout::Error
  # expected
end
client.close

exit(0)

