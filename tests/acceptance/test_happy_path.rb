# encoding: utf-8

require 'test/unit'
require 'environment'

class TestHappyPath < Test::Unit::TestCase
  def setup
    @env = Environment.new
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
        @env.nbd1.read(num*@env.blocksize, @env.blocksize),
        @env.file1.read(num*@env.blocksize, @env.blocksize)
      )
    end

    [124, 1200, 10028, 25488].each do |num|
      assert_equal(@env.nbd1.read(num, 4), @env.file1.read(num, 4))
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


  def test_mirror
    @env.writefile1( "f"*4 )
    @env.serve1

    @env.writefile2( "0"*4 )
    @env.listen2

    @env.nbd1.can_die
    stdout, stderr = @env.mirror12

    @env.nbd1.join

    assert_equal(@env.file1.read_original( 0, @env.blocksize ),
                 @env.file2.read( 0, @env.blocksize ) )
    assert @env.status2['has_control'], "destination didn't take control"
  end

end