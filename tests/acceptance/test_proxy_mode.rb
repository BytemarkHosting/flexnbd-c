require 'test/unit'
require 'environment'
require 'proxy_tests'

class TestProxyMode < Test::Unit::TestCase
  include ProxyTests

  def setup
    super
    @env = Environment.new
    @env.writefile1('f' * 16)
  end

  def teardown
    @env.cleanup
    super
  end
end
