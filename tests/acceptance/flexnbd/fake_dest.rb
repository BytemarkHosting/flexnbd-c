# encoding: utf-8

require 'socket'
require 'timeout'

require 'flexnbd/constants'

module FlexNBD
  class FakeDest

    class Client
      def initialize( sock )
        @sock = sock
      end


      def write_hello( opts = {} )
        self.class.write_hello( @sock, opts )
      end

      def read_request()
        self.class.read_request( @sock )
      end

      def write_error( handle )
        self.class.write_error( @sock, handle )
      end

      def close
        @sock.close
      end


      def self.write_hello( client_sock, opts={} )
        client_sock.write( "NBDMAGIC" )

        if opts[:magic] == :wrong
          client_sock.write( "\x00\x00\x42\x02\x81\x86\x12\x52" )
        else
          client_sock.write( "\x00\x00\x42\x02\x81\x86\x12\x53" )
        end

        if opts[:size] == :wrong
          8.times do client_sock.write rand(256).chr end
        else
          client_sock.write( "\x00\x00\x00\x00\x00\x00\x10\x00" )
        end

        client_sock.write( "\x00" * 128 )
      end



      def self.read_request( client_sock )
        req = client_sock.read(28)

        magic_s  = req[0  ... 4 ]
        type_s   = req[4  ... 8 ]
        handle_s = req[8  ... 16]
        from_s   = req[16 ... 24]
        len_s    = req[24 ... 28]

        {
          :magic  => magic_s,
          :type   => type_s.unpack("N").first,
          :handle => handle_s,
          :from   => parse_be64( from_s ),
          :len    => len_s.unpack( "N").first
        }
      end


      def self.parse_be64(str)
        raise "String is the wrong length: 8 bytes expected (#{str.length} received)" unless
          str.length == 8

        top, bottom = str.unpack("NN")
        (top << 32) + bottom
      end


      def self.write_error( client_sock, handle )
        client_sock.write( "\x67\x44\x66\x98")
        client_sock.write( "\x00\x00\x00\x01")
        client_sock.write( handle )
      end


    end # class Client


    def initialize( addr, port )
      @sock = self.class.serve( addr, port )
    end

    def accept( err_msg = "Timed out waiting for a connection", timeout = 2)
      Client.new( self.class.accept( @sock, err_msg, timeout ) )
    end

    def close
      @sock.close
    end


    def self.serve( addr, port )
      TCPServer.new( addr, port )
    end


    def self.accept( sock, err_msg, timeout )
      client_sock = nil

      begin
        Timeout.timeout(timeout) do
          client_sock = sock.accept
        end
      rescue Timeout::Error
        $stderr.puts err_msg
        exit 1
      end

      client_sock
    end


  end # module FakeDest
end # module FlexNBD
