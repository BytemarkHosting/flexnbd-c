#!/usr/bin/env ruby

# Simulate a server which has a disc of the wrong size attached: send
# a valid NBD hello with a random size, then check that we have see an
# EOF on read.

require 'flexnbd/fake_dest'
include FlexNBD
Thread.abort_on_exception = true

addr, port = *ARGV
server = FakeDest.new(addr, port)
client = server.accept

t = Thread.new do
  # The sender *should not reconnect.*  Since this is a first-pass
  # mirror attempt, the user will have been told that the mirror failed,
  # so it makes no sense to continue.  This means we have to invert the
  # sense of the exception.
  begin
    client2 = server.accept('Timed out waiting for a reconnection',
                            FlexNBD::MS_RETRY_DELAY_SECS + 1)
    client2.close
    raise 'Unexpected reconnection.'
  rescue Timeout::Error
  end
end

client.write_hello(size: :wrong)

t.join

# Now check that the source closed the first socket (yes, this was an
# actual bug)

raise "Didn't close socket" unless client.disconnected?

exit 0
