require 'test/unit'
require 'environment'
require 'flexnbd/fake_source'
require 'tempfile'

class TestServeMode < Test::Unit::TestCase
  def setup
    super
    @b = "\xFF".b
    @env = Environment.new
  end

  def teardown
    @env.cleanup
    teardown_msync_catcher
    super
  end

  def connect_to_server
    @env.writefile1('0')
    @env.serve1
    client = FlexNBD::FakeSource.new(@env.ip, @env.port1, 'Connecting to server failed')
    begin
      result = client.read_hello
      assert_equal 'NBDMAGIC', result[:passwd]
      assert_equal 0x00420281861253, result[:magic]
      assert_equal @env.file1.size, result[:size]
      # See src/common/nbdtypes.h for the various flags. At the moment we
      # support HAS_FLAGS (1), SEND_FLUSH (4), SEND_FUA (8)
      assert_equal (1 | 4 | 8), result[:flags]
      assert_equal "\x0" * 124, result[:reserved]
      yield client
    ensure
      begin
        client.close
      rescue StandardError
        nil
      end
    end
  end

  def setup_msync_catcher
    @msync_catcher = Tempfile.new('msync')
    ENV['MSYNC_CATCHER_OUTPUT'] = @msync_catcher.path
  end
  
  def parse_msync_output
    op = []
    until @msync_catcher.eof?
      op << @msync_catcher.readline.chomp.split(':').map do |e|
        e =~ /^\d+$/ ? e.to_i : e
      end
    end
    op
  end

  def teardown_msync_catcher
    @msync_catcher.close if @msync_catcher
    ENV.delete 'MSYNC_CATCHER_OUTPUT'
  end

  def test_bad_request_magic_receives_error_response
    connect_to_server do |client|
      # replace REQUEST_MAGIC with all 0s to make it look bad
      client.send_request(0, 'myhandle', 0, 0, "\x00\x00\x00\x00")
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 'myhandle', rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # The client should be disconnected now
      assert client.disconnected?, 'Server not disconnected'
    end
  end

  def test_long_write_on_top_of_short_write_is_respected
    connect_to_server do |client|
      # Start with a file of all-zeroes.
      client.write(0, "\x00" * @env.file1.size)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]

      client.write(0, @b)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]

      client.write(0, @b * 2)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
    end

    assert_equal @b * 2, @env.file1.read(0, 2)
  end

  def test_read_request_out_of_bounds_receives_error_response
    connect_to_server do |client|
      client.write_read_request(@env.file1.size, 4096)
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 'myhandle', rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # Ensure we're not disconnected by sending a request. We don't care about
      # whether the reply is good or not, here.
      client.write_read_request(0, 4096)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
    end
  end

  def test_write_request_out_of_bounds_receives_error_response
    connect_to_server do |client|
      client.write(@env.file1.size, "\x00" * 4096)
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 'myhandle', rsp[:handle]
      assert rsp[:error] != 0, "Server sent success reply back: #{rsp[:error]}"

      # Ensure we're not disconnected by sending a request. We don't care about
      # whether the reply is good or not, here.
      client.write(0, "\x00" * @env.file1.size)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
    end
  end

  def test_flush_is_accepted
    setup_msync_catcher
    connect_to_server do |client|
      client.flush
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
    end
    op = parse_msync_output
    assert_equal 1, op.count, 'Only one msync expected'
    assert_equal @env.blocksize, op.first[2], 'msync length wrong'
    assert_equal 6, op.first[3], 'msync called with incorrect flags'
  end

  def test_write_with_fua_is_accepted
    setup_msync_catcher
    page_size = Integer(`getconf PAGESIZE`)
    @env.blocksize = page_size * 10
    connect_to_server do |client|
      # Write somewhere in the third page
      pos = page_size * 3 + 100
      client.write_with_fua(pos, "\x00" * 33)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
    end

    op = parse_msync_output
    assert_equal 1, op.count, 'Only one msync expected'

    # Should be 100 + 33, as we've started writing 100 bytes into a page, for
    # 33 bytes
    assert_equal 133, op.first[2], 'msync length wrong'
    assert_equal 6, op.first[3], 'msync called with incorrect flags'
  end

end
