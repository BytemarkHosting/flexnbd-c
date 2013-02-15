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
        @sock.write( "NBDMAGIC" )

        if opts[:magic] == :wrong
          write_rand( @sock, 8 )
        else
          @sock.write( "\x00\x00\x42\x02\x81\x86\x12\x53" )
        end

        if opts[:size] == :wrong
          write_rand( @sock, 8 )
        else
          @sock.write( "\x00\x00\x00\x00\x00\x00\x10\x00" )
        end

        @sock.write( "\x00" * 128 )
      end


      def write_rand( sock, len )
        len.times do sock.write( rand(256).chr ) end
      end


      def read_request()
        req = @sock.read(28)

        magic_s  = req[0  ... 4 ]
        type_s   = req[4  ... 8 ]
        handle_s = req[8  ... 16]
        from_s   = req[16 ... 24]
        len_s    = req[24 ... 28]

        {
          :magic  => magic_s,
          :type   => type_s.unpack("N").first,
          :handle => handle_s,
          :from   => self.class.parse_be64( from_s ),
          :len    => len_s.unpack( "N").first
        }
      end

      def write_error( handle )
        write_reply( handle, 1 )
      end

      def disconnected?
        begin
          Timeout.timeout(2) do
            @sock.read(1) == nil
          end
        rescue Timeout::Error
          return false
        end
      end

      def write_reply( handle, err=0, opts={} )
        if opts[:magic] == :wrong
          write_rand( @sock, 4 )
        else
          @sock.write( ::FlexNBD::REPLY_MAGIC )
        end

        @sock.write( [err].pack("N") )
        @sock.write( handle )
      end


      def close
        @sock.close
      end


      def read_data( len )
        @sock.read( len )
      end

      def write_data( len )
        @sock.write( len )
      end


      def self.parse_be64(str)
        raise "String is the wrong length: 8 bytes expected (#{str.length} received)" unless
          str.length == 8

        top, bottom = str.unpack("NN")
        (top << 32) + bottom
      end


      def receive_mirror( opts = {} )
        write_hello()
        loop do
          req = read_request
          case req[:type]
          when 1
            read_data( req[:len] )
            write_reply( req[:handle] )
          when 65536
            write_reply( req[:handle], opts[:err] == :entrust ? 1 : 0 )
            break
          else
            raise "Unexpected request: #{req.inspect}"
          end
        end

        disc = read_request

        if disc[:type] == 2
          close
        else
          raise "Not a disconnect: #{req.inspect}"
        end
      end

    end # class Client


    def initialize( addr, port )
      @sock = TCPServer.new( addr, port )
    end


    def accept( err_msg = "Timed out waiting for a connection", timeout = 2)
      client_sock = nil

      begin
        Timeout.timeout(timeout) do
          client_sock = @sock.accept
        end
      rescue Timeout::Error
        raise Timeout::Error.new(err_msg)
      end

      client_sock

      Client.new( client_sock )
    end


    def close
      @sock.close
    end



  end # module FakeDest
end # module FlexNBD

