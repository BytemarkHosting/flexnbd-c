require 'socket'

# Noddy test class to exercise FlexNBD from the outside for testing.
#
class FlexNBD
  attr_reader :bin, :ctrl, :pid, :ip, :port

  def initialize(bin, ip, port)
    @bin  = bin
    @debug = "--verbose" # tests always run in debug mode!
    @valgrind = ENV['VALGRIND'] ? "valgrind " : ""
    @bin = "#{@valgrind}#{@bin}"
    raise "#{bin} not executable" unless File.executable?(bin)
    @ctrl = "/tmp/.flexnbd.ctrl.#{Time.now.to_i}.#{rand}"
    @ip = ip
    @port = port
  end

  def serve(file, *acl)
    File.unlink(ctrl) if File.exists?(ctrl)
    cmd ="#{@bin} serve "\
         "--addr #{ip} "\
         "--port #{port} "\
         "--file #{file} "\
         "--sock #{ctrl} "\
         "#{@debug} "\
         "#{acl.join(' ')}"
    #STDERR.print "#{cmd}\n"
    @pid = fork do
      exec(cmd)
    end
    while !File.socket?(ctrl)
      pid, status = Process.wait2(@pid, Process::WNOHANG)
      raise "server did not start (#{cmd})" if pid
      sleep 0.1
    end
    at_exit { kill }
  end

  def kill
    Process.kill("INT", @pid)
    Process.wait(@pid)
  end

  def read(offset, length)
    
    out = IO.popen("#{@bin} read "\
             "--addr #{ip} "\
             "--port #{port} "\
             "--from #{offset} "\
             "#{@debug} "\
             "--size #{length}","r") do |fh|
      fh.read
    end
    raise IOError.new "NBD read failed" unless $?.success?
    out
  end

  def write(offset, data)
    IO.popen("#{@bin} write "\
             "--addr #{ip} "\
             "--port #{port} "\
             "--from #{offset} "\
             "#{@debug} "\
             "--size #{data.length}","w") do |fh|
      fh.write(data)
    end
    raise IOError.new "NBD write failed" unless $?.success?
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

