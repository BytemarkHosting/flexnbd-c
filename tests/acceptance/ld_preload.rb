require 'tempfile'

#
# LdPreload is a little wrapper for using LD_PRELOAD loggers to pick up system
# calls when testing flexnbd.
#
module LdPreload
  #
  # This takes an object name, sets up a temporary log file, whose name is
  # recorded in the environment as OUTPUT_obj_name, where obj_name is the
  # name of the preload module to build and load.
  def with_ld_preload(obj_name)
    @ld_preload_logs ||= {}
    flunk 'Can only load a preload module once!' if @ld_preload_logs[obj_name]

    system("make -C ld_preloads/ #{obj_name}.o > /dev/null") ||
      flunk("Failed to build object #{obj_name}")
    orig_env = ENV['LD_PRELOAD']
    ENV['LD_PRELOAD'] = [orig_env, File.expand_path("./ld_preloads/#{obj_name}.o")].compact.join(' ')

    # Open the log, and stick it in a hash
    @ld_preload_logs[obj_name] = Tempfile.new(obj_name)
    ENV['OUTPUT_' + obj_name] = @ld_preload_logs[obj_name].path

    yield
  ensure
    if @ld_preload_logs[obj_name]
      @ld_preload_logs[obj_name].close
      @ld_preload_logs.delete(obj_name)
    end
    ENV['LD_PRELOAD'] = orig_env
  end

  def read_ld_preload_log(obj_name)
    lines = []
    lines << @ld_preload_logs[obj_name].readline.chomp until
      @ld_preload_logs[obj_name].eof?
    lines
  end

  #
  # The next to methods assume the log file has one entry per line, and that
  # each entry is a series of values separated by colons.
  #
  def parse_ld_preload_logs(obj_name)
    read_ld_preload_log(obj_name).map do |l|
      l.split(':').map { |i| i =~ /^\d+$/ ? i.to_i : i }
    end
  end

  def assert_func_call(loglines, args, msg)
    re = Regexp.new('^' + args.join(':'))
    assert(loglines.any? { |l| l.match(re) }, msg)
  end
end
