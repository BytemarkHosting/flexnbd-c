# encoding: utf-8

require 'test/unit'
require 'environment'
require 'flexnbd/constants'

class TestHappyPath < Test::Unit::TestCase
  def setup
    @env = Environment.new
  end

  def bin(str)
    FlexNBD.binary str
  end

  def teardown
    @env.nbd1.can_die(0)
    @env.nbd2.can_die(0)
    @env.cleanup
  end


  def test_read1
    @env.writefile1("f"*64)
    @env.serve1

    [0, 12, 63].each do |num|

      assert_equal(
        bin( @env.nbd1.read(num*@env.blocksize, @env.blocksize) ),
        bin( @env.file1.read(num*@env.blocksize, @env.blocksize) )
      )
    end

    [124, 1200, 10028, 25488].each do |num|
      assert_equal(bin(@env.nbd1.read(num, 4)), bin(@env.file1.read(num, 4)))
    end
  end

  # Check that we're not
  #
  def test_writeread1
    @env.writefile1("0"*64)
    @env.serve1

    [0, 12, 63].each do |num|
      data = "X"*@env.blocksize
      @env.nbd1.write(num*@env.blocksize, data)
      assert_equal(data, @env.file1.read(num*@env.blocksize, data.size))
      assert_equal(data, @env.nbd1.read(num*@env.blocksize, data.size))
    end
  end

  # Check that we're not overstepping or understepping where our writes end
  # up.
  #
  def test_writeread2
    @env.writefile1("0"*1024)
    @env.serve1

    d0 = "\0"*@env.blocksize
    d1 = "X"*@env.blocksize
    (0..63).each do |num|
      @env.nbd1.write(num*@env.blocksize*2, d1)
    end
    (0..63).each do |num|
      assert_equal(d0, @env.nbd1.read(((2*num)+1)*@env.blocksize, d0.size))
    end
  end


  def setup_to_mirror
    @env.writefile1( "f"*4 )
    @env.serve1

    @env.writefile2( "0"*4 )
    @env.listen2
  end


  def test_mirror
    @env.nbd1.can_die
    @env.nbd2.can_die(0)
    setup_to_mirror()

    stdout, stderr = @env.mirror12

    @env.nbd1.join
    @env.nbd2.join

    assert( File.file?( @env.filename1 ),
           "The source file was incorrectly deleted")
    assert_equal(@env.file1.read_original( 0, @env.blocksize ),
                 @env.file2.read( 0, @env.blocksize ) )
  end


  def test_mirror_unlink
    @env.nbd1.can_die(0)
    @env.nbd2.can_die(0)
    setup_to_mirror()

    assert File.file?( @env.filename1 )

    stdout, stderr = @env.mirror12_unlink

    assert_no_match( /unrecognized/, stderr )


    Timeout.timeout(10) do @env.nbd1.join end

    assert !File.file?( @env.filename1 )
  end



  def test_write_to_high_block
		# 
		# This test does not work on 32 bit platforms.
		#
		skip("Not relevant on 32-bit platforms") if ( ["a"].pack("p").size < 8 )

    # Create a large file, then try to write to somewhere after the 2G boundary
    @env.truncate1 "4G"
    @env.serve1

    @env.nbd1.write( 2**31+2**29, "12345678" )
    sleep(1)
    assert_equal "12345678", @env.nbd1.read( 2**31+2**29, 8 )
  end


  def test_set_acl
    # Just check that we get sane feedback here
    @env.writefile1( "f"*4 )
    @env.serve1

    _,stderr = @env.acl1("127.0.0.1")
    assert_no_match( /^(F|E):/, stderr )
  end


  def test_write_more_than_one_run
    one_mb = 2**20
    data = "\0" * 256 * one_mb

    File.open(@env.filename1, "wb") do |f| f.write( "1" * 256 * one_mb ) end

    @env.serve1
    sleep 5
    @env.write1( data )
    @env.nbd1.can_die(0)
    @env.nbd1.kill

    i = 0
    File.open(@env.filename1, "rb") do |f|
      while mb = f.read( one_mb )
        unless "\0"*one_mb == mb
          msg = "Read non-zeros after offset %x:\n"%(i * one_mb)
          msg += `hexdump #{@env.filename1} | head -n5`
          fail msg
        end
        i += 1
      end
    end
  end

end

