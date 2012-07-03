# encoding: utf-8

require 'test/unit'
require 'environment'


class TestSourceErrorHandling < Test::Unit::TestCase

  def setup
    @env = Environment.new
    @env.writefile1( "f" * 4 )
    @env.serve1
  end


  def teardown
    @env.nbd1.can_die(0)
    @env.cleanup
  end


  def test_failure_to_connect_reported_in_mirror_cmd_response
    stdout, stderr = @env.mirror12_unchecked
    assert_match( /failed to connect/, stderr )
  end


  def test_destination_hangs_after_connect_reports_error_at_source
    run_fake( "dest/hang_after_connect" )

    stdout, stderr = @env.mirror12_unchecked
    assert_match( /Remote server failed to respond/, stderr )
    assert_success
  end


  def test_destination_rejects_connection_reports_error_at_source
    run_fake( "dest/reject_acl" )

    stdout, stderr = @env.mirror12_unchecked
    assert_match /Mirror was rejected/, stderr
    assert_success
  end

  def test_wrong_size_causes_disconnect
    run_fake( "dest/hello_wrong_size" )
    stdout, stderr = @env.mirror12_unchecked
    assert_match /Remote size does not match local size/, stderr
    assert_success
  end


  def test_wrong_magic_causes_disconnect
    run_fake( "dest/hello_wrong_magic" )
    stdout, stderr = @env.mirror12_unchecked
    assert_match /Mirror was rejected/, stderr
    assert_success "dest/hello_wrong_magic fake failed"
  end


  def test_disconnect_after_hello_causes_retry
    run_fake( "dest/close_after_hello" )
    stdout, stderr = @env.mirror12_unchecked
    assert_match( /Mirror started/, stdout )

    assert_success
  end


  def test_write_times_out_causes_retry
    run_fake( "dest/hang_after_write" )
    stdout, stderr = @env.mirror12_unchecked

    assert_success
  end


  def test_rejected_write_causes_retry
    run_fake( "dest/error_on_write" )
    stdout, stderr = @env.mirror12_unchecked
    assert_success
  end


  def test_disconnect_before_write_reply_causes_retry
    run_fake( "dest/close_after_write" )
    @env.mirror12_unchecked
    assert_success
  end


  private
  def run_fake(name)
    @env.run_fake( name, @env.ip, @env.port2 )
  end

  def assert_success( msg=nil )
    assert @env.fake_reports_success, msg || "Fake failed"
  end


end # class TestSourceErrorHandling
