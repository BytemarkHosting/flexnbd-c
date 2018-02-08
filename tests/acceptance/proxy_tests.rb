
require 'flexnbd/fake_source'
require 'flexnbd/fake_dest'

module ProxyTests
  def b
    "\xFF".b
  end

  def with_proxied_client(override_size = nil)
    @env.serve1 unless @server_up
    @env.proxy2 unless @proxy_up
    @env.nbd2.can_die(0)
    client = FlexNBD::FakeSource.new(@env.ip, @env.port2, "Couldn't connect to proxy")
    begin
      result = client.read_hello
      assert_equal 'NBDMAGIC', result[:passwd]
      assert_equal override_size || @env.file1.size, result[:size]

      yield client
    ensure
      begin
        client.close
      rescue StandardError
        nil
      end
    end
  end

  def test_exits_with_error_when_cannot_connect_to_upstream_on_start
    assert_raises(RuntimeError) { @env.proxy1 }
  end

  def test_read_requests_successfully_proxied
    with_proxied_client do |client|
      (0..3).each do |n|
        offset = n * 4096
        client.write_read_request(offset, 4096, 'myhandle')
        rsp = client.read_response

        assert_equal ::FlexNBD::REPLY_MAGIC, rsp[:magic]
        assert_equal 'myhandle', rsp[:handle]
        assert_equal 0, rsp[:error]

        orig_data = @env.file1.read(offset, 4096)
        data = client.read_raw(4096)

        assert_equal 4096, orig_data.size
        assert_equal 4096, data.size

        assert_equal(orig_data, data,
                     "Returned data does not match on request #{n + 1}")
      end
    end
  end

  def test_write_requests_successfully_proxied
    with_proxied_client do |client|
      (0..3).each do |n|
        offset = n * 4096
        client.write(offset, b * 4096)
        rsp = client.read_response

        assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
        assert_equal 'myhandle', rsp[:handle]
        assert_equal 0, rsp[:error]

        data = @env.file1.read(offset, 4096)

        assert_equal((b * 4096), data, "Data not written correctly (offset is #{n})")
      end
    end
  end

  def test_write_request_past_end_of_disc_returns_to_client
    with_proxied_client do |client|
      n = 1000
      offset = n * 4096
      client.write(offset, b * 4096)
      rsp = client.read_response

      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 'myhandle', rsp[:handle]
      # NBD protocol say ENOSPC (28) in this situation
      assert_equal 28, rsp[:error]
    end
  end

  def make_fake_server
    server = FlexNBD::FakeDest.new(@env.ip, @env.port1)
    @server_up = true

    # We return a thread here because accept() and connect() both block for us
    Thread.new do
      sc = server.accept # just tell the supervisor we're up
      sc.write_hello

      [server, sc]
    end
  end

  def test_read_request_retried_when_upstream_dies_partway
    maker = make_fake_server

    with_proxied_client(4096) do |client|
      server, sc1 = maker.value

      # Send the read request to the proxy
      client.write_read_request(0, 4096)

      # ensure we're given the read request
      req1 = sc1.read_request
      assert_equal ::FlexNBD::REQUEST_MAGIC, req1[:magic]
      assert_equal ::FlexNBD::REQUEST_READ, req1[:type]
      assert_equal 0, req1[:from]
      assert_not_equal 0, req1[:len]

      # Kill the server again, now we're sure the read request has been sent once
      sc1.close

      # We expect the proxy to reconnect without our client doing anything.
      sc2 = server.accept
      sc2.write_hello

      # And once reconnected, it should resend an identical request.
      req2 = sc2.read_request
      assert_equal req1, req2

      # The reply should be proxied back to the client.
      sc2.write_reply(req2[:handle])
      sc2.write_data(b * 4096)

      # Check it to make sure it's correct
      rsp = Timeout.timeout(15) { client.read_response }
      assert_equal ::FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
      assert_equal req1[:handle], rsp[:handle]

      data = client.read_raw(4096)
      assert_equal((b * 4096), data, 'Wrong data returned')

      sc2.close
      server.close
    end
  end

  def test_write_request_retried_when_upstream_dies_partway
    maker = make_fake_server

    with_proxied_client(4096) do |client|
      server, sc1 = maker.value

      # Send the read request to the proxy
      client.write(0, (b * 4096))

      # ensure we're given the read request
      req1 = sc1.read_request
      assert_equal ::FlexNBD::REQUEST_MAGIC, req1[:magic]
      assert_equal ::FlexNBD::REQUEST_WRITE, req1[:type]
      assert_equal 0, req1[:from]
      assert_equal 4096, req1[:len]
      data1 = sc1.read_data(4096)
      assert_equal((b * 4096), data1, 'Data not proxied successfully')

      # Kill the server again, now we're sure the read request has been sent once
      sc1.close

      # We expect the proxy to reconnect without our client doing anything.
      sc2 = server.accept
      sc2.write_hello

      # And once reconnected, it should resend an identical request.
      req2 = sc2.read_request
      assert_equal req1, req2
      data2 = sc2.read_data(4096)
      assert_equal data1, data2

      # The reply should be proxied back to the client.
      sc2.write_reply(req2[:handle])

      # Check it to make sure it's correct
      rsp = Timeout.timeout(15) { client.read_response }
      assert_equal ::FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
      assert_equal req1[:handle], rsp[:handle]

      sc2.close
      server.close
    end
  end

  def test_only_one_client_can_connect_to_proxy_at_a_time
    with_proxied_client do |_client|
      c2 = nil
      assert_raises(Timeout::Error) do
        Timeout.timeout(1) do
          c2 = FlexNBD::FakeSource.new(@env.ip, @env.port2, "Couldn't connect to proxy (2)")
          c2.read_hello
        end
      end
      if c2
        begin
          c2.close
        rescue StandardError
          nil
        end
      end
    end
  end
end
