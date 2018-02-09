require 'test/unit'
require 'environment'
require 'ld_preload'
require 'proxy_tests'

class TestProxyMode < Test::Unit::TestCase

  include LdPreload
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
