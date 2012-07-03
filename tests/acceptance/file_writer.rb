# Noddy test class for writing files to disc in predictable patterns
# in order to test FlexNBD.
#
class FileWriter
  def initialize(filename, blocksize)
    @fh = File.open(filename, "w+")
    @blocksize = blocksize
    @pattern = ""
  end

  # We write in fixed block sizes, given by "blocksize"
  #    _ means skip a block
  #    0 means write a block full of zeroes
  #    f means write a block with the file offset packed every 4 bytes
  #
  def write(data)
    @pattern += data

    data.split("").each do |code|
      if code == "_"
        @fh.seek(@blocksize, IO::SEEK_CUR)
      else
        @fh.write(data(code))
      end
    end
    @fh.flush
    self
  end


  # Returns what the data ought to be at the given offset and length
  #
  def read_original( off, len )
    patterns = @pattern.split( "" )
    patterns.zip( (0...patterns.length).to_a ).
      map { |blk, blk_off|
      data(blk, blk_off)
    }.join[off...(off+len)]
  end

  # Read what's actually in the file
  #
  def read(off, len)
    @fh.seek(off, IO::SEEK_SET)
    @fh.read(len)
  end

  def untouched?(offset, len)
    read(offset, len) == read_original(offset, len)
  end

  def close
    @fh.close
    nil
  end

  protected

  def data(code, at=@fh.tell)
    case code
      when "0", "_"
        "\0" * @blocksize
      when "X"
        "X" * @blocksize
      when "f"
        r = ""
        (@blocksize/4).times do
          r += [at].pack("I")
          at += 4
        end
        r
      else
        raise "Unknown character '#{block}'"
    end
  end

end

if __FILE__==$0
  require 'tempfile'
  require 'test/unit'

  class FileWriterTest < Test::Unit::TestCase
    def test_read_original_zeros
      Tempfile.open("test_read_original_zeros") do |tempfile|
        tempfile.close
        file = FileWriter.new( tempfile.path, 4096 )
        file.write( "0" )
        assert_equal file.read( 0, 4096 ), file.read_original( 0, 4096 )
        assert( file.untouched?(0,4096) , "Untouched file was touched." )
      end
    end

    def test_read_original_offsets
      Tempfile.open("test_read_original_offsets") do |tempfile|
        tempfile.close
        file = FileWriter.new( tempfile.path, 4096 )
        file.write( "f" )
        assert_equal file.read( 0, 4096 ), file.read_original( 0, 4096 )
        assert( file.untouched?(0,4096) , "Untouched file was touched." )
      end
    end

    def test_file_size
      Tempfile.open("test_file_size") do |tempfile|
        tempfile.close
        file = FileWriter.new( tempfile.path, 4096 )
        file.write( "f" )
        assert_equal 4096, File.stat( tempfile.path ).size
      end
    end

    def test_read_original_size
      Tempfile.open("test_read_original_offsets") do |tempfile|
        tempfile.close
        file = FileWriter.new( tempfile.path, 4)
        file.write( "f"*4 )
        assert_equal 4, file.read_original(0, 4).length
      end
    end
  end
end

