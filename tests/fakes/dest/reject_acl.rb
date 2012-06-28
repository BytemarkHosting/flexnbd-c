#!/usr/bin/env ruby

# Accept a connection, then immediately close it.  This simulates an ACL rejection.

require 'flexnbd/fake_dest'
include FlexNBD::FakeDest

serve_sock = serve( *ARGV )
accept( serve_sock, "Timed out waiting for a connection" ).close

serve_sock.close

exit(0)
