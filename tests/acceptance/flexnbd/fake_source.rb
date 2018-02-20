require 'socket'
require 'timeout'
require 'flexnbd/constants'

module FlexNBD
  class FakeSource
    def initialize(addr, port, err_msg, source_addr = nil, source_port = 0)
      timing_out(2, err_msg) do
        begin
          @sock = if source_addr
                    TCPSocket.new(addr, port, source_addr, source_port)
                  else
                    TCPSocket.new(addr, port)
                  end
        rescue Errno::ECONNREFUSED
          warn 'Connection refused, retrying'
          sleep(0.2)
          retry
        end
      end
    end

    def close
      @sock.close
    end

    def read_hello
      timing_out(::FlexNBD::MS_HELLO_TIME_SECS,
                 'Timed out waiting for hello.') do
        raise 'No hello.' unless (hello = @sock.read(152)) &&
                                 hello.length == 152

        passwd_s = hello[0..7]
        magic    = hello[8..15].unpack('Q>').first
        size     = hello[16..23].unpack('Q>').first
        flags    = hello[24..27].unpack('L>').first
        reserved = hello[28..-1]

        return { passwd: passwd_s, magic: magic, size: size, flags: flags, reserved: reserved }
      end
    end

    def send_request(type, handle = 'myhandle', from = 0, len = 0, magic = REQUEST_MAGIC, flags = 0)
      raise 'Bad handle' unless handle.length == 8

      @sock.write(magic)
      @sock.write([flags].pack('n'))
      @sock.write([type].pack('n'))
      @sock.write(handle)
      @sock.write([n64(from)].pack('q'))
      @sock.write([len].pack('N'))
    end

    def write_write_request(from, len, handle = 'myhandle')
      send_request(1, handle, from, len)
    end

    def write_write_request_with_fua(from, len, handle = 'myhandle')
      send_request(1, handle, from, len, REQUEST_MAGIC, 1)
    end

    def write_flush_request(handle = 'myhandle')
      send_request(3, handle, 0, 0)
    end

    def write_entrust_request(handle = 'myhandle')
      send_request(65_536, handle)
    end

    def write_disconnect_request(handle = 'myhandle')
      send_request(2, handle)
    end

    def write_read_request(from, len, _handle = 'myhandle')
      send_request(0, 'myhandle', from, len)
    end

    def write_data(data)
      @sock.write(data)
    end

    # Handy utility
    def read(from, len)
      timing_out(2, 'Timed out reading') do
        send_request(0, 'myhandle', from, len)
        read_raw(len)
      end
    end

    def read_raw(len)
      @sock.read(len)
    end

    def send_mirror
      read_hello
      write(0, '12345678')
      read_response
      write_disconnect_request
      close
    end

    def write(from, data)
      write_write_request(from, data.length)
      write_data(data)
    end

    def write_with_fua(from, data)
      write_write_request_with_fua(from, data.length)
      write_data(data)
    end

    def flush
      write_flush_request
    end

    def read_response
      magic = @sock.read(4)
      error_s = @sock.read(4)
      handle = @sock.read(8)

      {
        magic: magic,
        error: error_s.unpack('N').first,
        handle: handle
      }
    end

    def disconnected?
      result = nil
      Timeout.timeout(2) { result = @sock.read(1).nil? }
      result
    end

    def timing_out(time, msg)
      Timeout.timeout(time) do
        yield
      end
    rescue Timeout::Error
      warn msg
      exit 1
    end

    private

    # take a 64-bit number, turn it upside down (due to :
    #   http://blade.nagaokaut.ac.jp/cgi-bin/scat.rb/ruby/ruby-core/11920
    # )
    def n64(b)
      ((b & 0xff00000000000000) >> 56) |
        ((b & 0x00ff000000000000) >> 40) |
        ((b & 0x0000ff0000000000) >> 24) |
        ((b & 0x000000ff00000000) >> 8)  |
        ((b & 0x00000000ff000000) << 8)  |
        ((b & 0x0000000000ff0000) << 24) |
        ((b & 0x000000000000ff00) << 40) |
        ((b & 0x00000000000000ff) << 56)
    end
  end # class FakeSource
end # module FlexNBD
