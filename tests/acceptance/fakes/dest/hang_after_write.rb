#!/usr/bin/env ruby
# Open a socket, say hello, receive a write, then sleep for >
# MS_REQUEST_LIMIT_SECS seconds. This should tell the source that the
# write has gone MIA, and we expect a reconnect.

require 'flexnbd/fake_dest'
include FlexNBD

addr, port = *ARGV
server = FakeDest.new(addr, port)
client1 = server.accept(server)
client1.write_hello
client1.read_request

t = Thread.start do
  client2 = server.accept('Timed out waiting for a reconnection',
                          FlexNBD::MS_REQUEST_LIMIT_SECS + 2)
  client2.close
end

sleep_time = if ENV.key?('FLEXNBD_MS_REQUEST_LIMIT_SECS')
               ENV['FLEXNBD_MS_REQUEST_LIMIT_SECS'].to_f
             else
               FlexNBD::MS_REQUEST_LIMIT_SECS
end

sleep(sleep_time + 2.0)
client1.close

t.join

server.close
exit(0)
