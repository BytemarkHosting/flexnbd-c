# encoding: utf-8

require 'socket'
require 'timeout'

require 'flexnbd/constants'

module FlexNBD
  module FakeDest

    def serve( addr, port )
      TCPServer.new( addr, port )
    end


    def accept( sock, err_msg, timeout=2 )
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


    def write_hello( client_sock, opts={} )
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

  end
end # module FlexNBD
