# encoding: utf-8

require 'test/unit'
require 'environment'

class TestDestErrorHandling  < Test::Unit::TestCase

  def setup
    @env = Environment.new
    @env.writefile1( "0" * 4 )
    @env.listen1
  end

  def teardown
    @env.cleanup
  end


  def test_hello_blocked_by_disconnect_causes_error_not_fatal
    run_fake( "source/close_after_connect" )
    assert_no_control
  end


  def test_hello_goes_astray_causes_timeout_error
    run_fake( "source/hang_after_hello" )
    assert_no_control
  end


  def test_disconnect_after_hello_causes_error_not_fatal
    run_fake( "source/close_after_hello" )
    assert_no_control
  end


  def test_partial_read_causes_error
    run_fake( "source/close_mid_read" )
  end

  def test_double_connect_during_hello
    run_fake( "source/connect_during_hello" )
  end


  def test_acl_rejection
    @env.acl1("127.0.0.1")
    run_fake( "source/connect_from_banned_ip")
  end


  def test_bad_write
    run_fake( "source/write_out_of_range" )
  end


  def test_disconnect_before_write_data_causes_error
    run_fake( "source/close_after_write" )
  end

  def test_disconnect_before_entrust_reply_causes_error
    run_fake( "source/close_after_entrust" )
  end


  def test_disconnect_before_write_reply_causes_error
    # Note that this is an odd case: writing the reply doesn't fail.
    # The test passes because the next attempt by flexnbd to read a
    # request returns EOF.
    run_fake( "source/close_after_write_data" )
  end


  def test_disconnect_after_entrust_reply_causes_error
    @env.nbd1.can_die(0)
    # This fake runs a failed migration then a succeeding one, so we
    # expect the destination to take control.
    run_fake( "source/close_after_entrust_reply" )
    assert_control
  end


  def test_cant_rebind_dies
    @env.nbd1.can_die(6)
    run_fake( "source/successful_transfer" )
  end


  private
  def run_fake( name )
    @env.run_fake( name, @env.ip, @env.port1, @env.ip, @env.rebind_port1 )
    assert @env.fake_reports_success, "#{name} failed."
  end

  def status
    stat, _ = @env.status1
    stat
  end

  def assert_no_control
    assert !status['has_control'],  "Thought it had control"
  end

  def assert_control
    assert status['has_control'], "Didn't think it had control"
  end

end # class TestDestErrorHandling
