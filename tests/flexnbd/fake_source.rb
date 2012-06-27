# encoding: utf-8

require 'socket'
require "timeout"
require 'flexnbd/constants'

module FlexNBD
  module FakeSource

    def connect( addr, port, err_msg )
      timing_out( 2, err_msg ) do
        TCPSocket.open( addr, port )
      end
    end


    def read_hello( client_sock )
      timing_out( FlexNBD::MS_HELLO_TIME_SECS,
                  "Timed out waiting for hello." ) do
          fail "No hello." unless (hello = client_sock.read( 152 )) &&
            hello.length==152
        hello
      end
    end


    def timing_out( time, msg )
      begin
        Timeout.timeout( time ) do
          yield
        end
      rescue Timeout::Error
        $stderr.puts msg
        exit 1
      end
    end


  end # module FakeSource
end # module FlexNBD
