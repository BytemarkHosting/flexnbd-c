# Noddy test class for writing files to disc in predictable patterns
# in order to test FlexNBD.
#
class TestFileWriter
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
  def read_original(off, len)
    r=""
    current = 0
    @pattern.split("").each do |block|
      if off >= current && (off+len) < current + blocksize
        current += data(block, current)[
          current-off..(current+blocksize)-(off+len)
        ]
      end
      current += @blocksize
    end
    r
  end
  
  # Read what's actually in the file
  #
  def read(off, len)
    @fh.seek(off, IO::SEEK_SET)
    @fh.read(len)
  end
  
  def untouched?(offset, len)
    read(off, len) == read_original(off, len)
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

