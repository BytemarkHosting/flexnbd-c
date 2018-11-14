
require 'flexnbd/fake_source'
require 'flexnbd/fake_dest'
require 'ld_preload'

module ProxyTests

  include LdPreload

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

    with_ld_preload('setsockopt_logger') do
      with_proxied_client(4096) do |client|
        server, sc1 = maker.value

        # Send the read request to the proxy
        client.write(0, (b * 4096))

        # ensure we're given the write request
        req1 = sc1.read_request
        assert_equal ::FlexNBD::REQUEST_MAGIC, req1[:magic]
        assert_equal ::FlexNBD::REQUEST_WRITE, req1[:type]
        assert_equal 0, req1[:from]
        assert_equal 4096, req1[:len]
        data1 = sc1.read_data(4096)
        assert_equal((b * 4096), data1, 'Data not proxied successfully')

        # Read the setsockopt logs, so we can check that TCP_NODELAY is re-set
        # later
        read_ld_preload_log('setsockopt_logger')

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

        # Check TCP_NODELAY was set on the upstream socket
        log = read_ld_preload_log('setsockopt_logger')
        assert_func_call(log,
                         ['setsockopt', 3,
                          Socket::SOL_TCP, Socket::TCP_NODELAY, 1, 0],
                         'TCP_NODELAY not set on upstream fd 3')
      end
    end
  end

  def test_write_request_retried_when_upstream_times_out_during_write_phase
    ENV['FLEXNBD_UPSTREAM_TIMEOUT'] = '4'
    maker = make_fake_server

    with_ld_preload('setsockopt_logger') do
      with_proxied_client(4096) do |client|
        server, sc1 = maker.value

        # Guess an approprate request size, based on the send buffer size.
        sz = sc1.getsockopt(Socket::SOL_SOCKET, Socket::SO_SNDBUF).int * 4
        data1 = (b * sz)

        # Send the read request to the proxy
        client.write(0, data1)

        # ensure we're given the write request
        req1 = sc1.read_request
        assert_equal ::FlexNBD::REQUEST_MAGIC, req1[:magic]
        assert_equal ::FlexNBD::REQUEST_WRITE, req1[:type]
        assert_equal 0, req1[:from]
        assert_equal data1.size, req1[:len]

        # We do not read it at this point, as we want the proxy to be waiting
        # in the WRITE_UPSTREAM state.

        # Need to sleep longer than the timeout set above
        sleep 5

        # Check the number of bytes that can be read from the socket without
        # blocking.  If this equal to the size of the original request, then
        # the whole request has been buffered.  If this is the case, then the
        # proxy will not time-out in the WRITE_UPSTREAM statem which is what
        # we're trying to test.
        assert sc1.nread < sz, 'Request from proxy completely buffered. Test is useless'

        # Kill the server now that the timeout has happened.
        sc1.close

        # We expect the proxy to reconnect without our client doing anything.
        sc2 = server.accept
        sc2.write_hello

        # And once reconnected, it should resend an identical request.
        req2 = sc2.read_request
        assert_equal req1, req2

        # And now lets read the data to make sure we get it all.
        data2 = sc2.read_data(req2[:len])
        assert_equal data1, data2

        sc2.close
        server.close
      end
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

  def test_maximum_write_request_size
    # Defined in src/common/nbdtypes.h
    nbd_max_block_size = 32 * 1024 * 1024
    @env.writefile1('0' * 40 * 1024)
    with_proxied_client do |client|
      # This will crash with EPIPE if the proxy dies.
      client.write(0, b * nbd_max_block_size)
      rsp = client.read_response
      assert_equal FlexNBD::REPLY_MAGIC, rsp[:magic]
      assert_equal 0, rsp[:error]
    end
  end
end
