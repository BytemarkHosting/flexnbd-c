#!/usr/bin/env ruby
# encoding: utf-8

# Open a server, accept a client, then we expect a single write
# followed by an entrust. Disconnect after the entrust. We expect a
# reconnection followed by a full mirror.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port, src_pid = *ARGV
server = FakeDest.new( addr, port )
client = server.accept

client.write_hello
write_req = client.read_request
data = client.read_data( write_req[:len] )
client.write_reply( write_req[:handle], 0 )

entrust_req = client.read_request
fail "Not an entrust" unless entrust_req[:type] == 65536
client.close

client2 = server.accept
client2.receive_mirror


exit(0)

