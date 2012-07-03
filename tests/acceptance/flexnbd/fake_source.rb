# encoding: utf-8

require 'socket'
require "timeout"
require 'flexnbd/constants'

module FlexNBD
  class FakeSource

    def initialize( addr, port, err_msg, source_addr=nil, source_port=0 )
      timing_out( 2, err_msg ) do
        @sock = TCPSocket.new( addr, port, source_addr, source_port )
      end
    end


    def close
      @sock.close
    end


    def read_hello()
      timing_out( FlexNBD::MS_HELLO_TIME_SECS,
                  "Timed out waiting for hello." ) do
          fail "No hello." unless (hello = @sock.read( 152 )) &&
            hello.length==152
        hello
      end
    end


    def write_write_request( from, len, handle="myhandle" )
      fail "Bad handle" unless handle.length == 8

      @sock.write( "\x25\x60\x95\x13" )
      @sock.write( "\x00\x00\x00\x01" )
      @sock.write( handle )
      @sock.write( "\x0"*4 )
      @sock.write( [from].pack( 'N' ) )
      @sock.write( [len ].pack( 'N' ) )
    end


    def read_response
      magic = @sock.read(4)
      error_s = @sock.read(4)
      handle = @sock.read(8)

      {
        :magic => magic,
        :error => error_s.unpack("N"),
        :handle => handle
      }
    end


    def ensure_disconnected
      Timeout.timeout( 2 ) do
        @sock.read(1)
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


  end # class FakeSource
end # module FlexNBD
