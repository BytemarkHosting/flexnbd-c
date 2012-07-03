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


  private
  def run_fake( name )
    @env.run_fake( name, @env.ip, @env.port1 )
    assert @env.fake_reports_success, "#{name} failed."
  end

  def assert_no_control
    status, stderr = @env.status1
    assert !status['has_control'],  "Thought it had control"
  end


end # class TestDestErrorHandling
