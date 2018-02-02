#!/usr/bin/env ruby
# Open a server, accept a client, then we expect a single write
# followed by an entrust.  However, we disconnect after the write so
# the entrust will fail.  We don't expect a reconnection: the sender
# can't reliably spot a failed send.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, src_pid = *ARGV
server = FakeDest.new(addr, port)
client = server.accept

client.write_hello
req = client.read_request
data = client.read_data(req[:len])

Process.kill('STOP', src_pid.to_i)
client.write_reply(req[:handle], 0)
client.close
Process.kill('CONT', src_pid.to_i)

exit(0)
