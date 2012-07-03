# encoding: utf-8

require 'socket'
require "timeout"
require 'flexnbd/constants'

module FlexNBD
  module FakeSource

    def connect( addr, port, err_msg, source_addr=nil, source_port=0 )
      timing_out( 2, err_msg ) do
        TCPSocket.new( addr, port, source_addr, source_port )
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


    def write_write_request( client_sock, from, len, handle )
      fail "Bad handle" unless handle.length == 8

      client_sock.write( "\x25\x60\x95\x13" )
      client_sock.write( "\x00\x00\x00\x01" )
      client_sock.write( handle )
      client_sock.write( "\x0"*4 )
      client_sock.write( [from].pack( 'N' ) )
      client_sock.write( [len ].pack( 'N' ) )
    end


    def read_response( client_sock )
      magic = client_sock.read(4)
      error_s = client_sock.read(4)
      handle = client_sock.read(8)

      {
        :magic => magic,
        :error => error_s.unpack("N"),
        :handle => handle
      }
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
