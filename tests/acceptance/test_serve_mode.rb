require 'test/unit'
require 'environment'
require 'flexnbd/fake_source'

class TestServeMode < Test::Unit::TestCase

  def setup
    super
    @env = Environment.new
    @env.writefile1( "0" )
    @env.serve1
  end

  def teardown
    @env.cleanup
    super
  end

  def connect_to_server
    client = FlexNBD::FakeSource.new(@env.ip, @env.port1, "Connecting to server failed")
    begin
      result = client.read_hello
      assert_equal "NBDMAGIC", result[:magic]
      assert_equal @env.file1.size, result[:size]
      yield client
    ensure
      client.close rescue nil
    end
  end

  def test_bad_request_magic_receives_error_response
    connect_to_server do |client|

      # replace REQUEST_MAGIC with all 0s to make it look bad
      client.send_request( 0, "myhandle", 0, 0, "\x00\x00\x00\x00" )
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal "myhandle", rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # The client should be disconnected now
      assert client.disconnected?, "Server not disconnected"
    end
  end


  def test_read_request_out_of_bounds_receives_error_response
    connect_to_server do |client|
      client.write_read_request( @env.file1.size, 4096 )
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal "myhandle", rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # Ensure we're not disconnected by sending a request. We don't care about
      # whether the reply is good or not, here.
      client.write_read_request( 0, 4096 )
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
    end
  end

  def test_write_request_out_of_bounds_receives_error_response
    connect_to_server do |client|
      client.write( @env.file1.size, "\x00" * 4096 )
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal "myhandle", rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # Ensure we're not disconnected by sending a request. We don't care about
      # whether the reply is good or not, here.
      client.write( 0, "\x00" * @env.file1.size )
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
    end

  end



end

