require 'test/unit'
require 'environment'
require 'proxy_tests'


class TestPrefetchProxyMode < Test::Unit::TestCase
  include ProxyTests

  def setup
    super
    @env = Environment.new
    @env.prefetch_proxy!
    @env.writefile1( "f" * 16 )
  end

  def teardown
    @env.cleanup
    super
  end
end


