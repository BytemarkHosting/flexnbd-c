require 'socket'
require 'thread'

Thread.abort_on_exception = true

# Noddy test class to exercise FlexNBD from the outside for testing.
#
class FlexNBD
  attr_reader :bin, :ctrl, :pid, :ip, :port

  def initialize(bin, ip, port)
    @bin  = bin
    @debug = `#{@bin} serve --help` =~ /--verbose/ ? "--verbose" : ""
    @valgrind = ENV['VALGRIND'] ? "valgrind " : ""
    @bin = "#{@valgrind}#{@bin}"
    raise "#{bin} not executable" unless File.executable?(bin)
    @ctrl = "/tmp/.flexnbd.ctrl.#{Time.now.to_i}.#{rand}"
    @ip = ip
    @port = port
    @kill = false
  end

  def debug?
    !@debug.empty?
  end

  def debug( msg )
    $stderr.puts msg if debug?
  end


  def serve_cmd( file, acl )
    "#{@bin} serve "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--file #{file} "\
      "--sock #{ctrl} "\
      "#{@debug} "\
      "#{acl.join(' ')}"
  end


  def read_cmd( offset, length )
    "#{@bin} read "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--from #{offset} "\
      "#{@debug} "\
      "--size #{length}"
  end


  def write_cmd( offset, data )
    "#{@bin} write "\
      "--addr #{ip} "\
      "--port #{port} "\
      "--from #{offset} "\
      "#{@debug} "\
      "--size #{data.length}"
  end


  def serve(file, *acl)
    File.unlink(ctrl) if File.exists?(ctrl)
    cmd =serve_cmd( file, acl )
    debug( cmd )

    @pid = fork do exec(cmd) end
    start_wait_thread( @pid )

    while !File.socket?(ctrl)
      pid, status = Process.wait2(@pid, Process::WNOHANG)
      raise "server did not start (#{cmd})" if pid
      sleep 0.1
    end
    at_exit { kill }
  end

  def start_wait_thread( pid )
    Thread.start do
      Process.waitpid2( pid )
      unless @kill
        $stderr.puts "flexnbd quit"
        fail "flexnbd quit early"
      end
    end
  end


  def kill
    @kill = true
    Process.kill("INT", @pid)
  end

  def read(offset, length)
    cmd = read_cmd( offset, length )
    debug( cmd )

    IO.popen(cmd) do |fh|
      return fh.read
    end
    raise "read failed" unless $?.success?
  end

  def write(offset, data)
    cmd = write_cmd( offset, data )
    debug( cmd )
    
    IO.popen(cmd, "w") do |fh|
      fh.write(data)
    end
    raise "write failed" unless $?.success?
    nil
  end

  def mirror(bandwidth=nil, action=nil)
    control_command("mirror", ip, port, ip, bandwidth, action)
  end

  def acl(*acl)
    control_command("acl", *acl)
  end

  def status
  end

  protected
  def control_command(*args)
    raise "Server not running" unless @pid
    args = args.compact
    UNIXSocket.open(@ctrl) do |u|
      u.write(args.join("\n") + "\n")
      code, message = u.readline.split(": ", 2)
      return [code, message]
    end
  end
end

