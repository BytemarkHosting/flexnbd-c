require 'socket'

# Noddy test class to exercise FlexNBD from the outside for testing.
#
class FlexNBD
  attr_reader :bin, :ctrl, :pid, :ip, :port
  
  def initialize(bin, ip, port)
    @bin  = bin
    raise "#{bin} not executable" unless File.executable?(bin)
    @ctrl = "/tmp/.flexnbd.ctrl.#{Time.now.to_i}.#{rand}"
    @ip = ip
    @port = port
  end
  
  def serve(ip, port, file, *acl)
    @pid = fork do
      cmd ="#{@bin} serve "\
           "--addr #{ip} "\
           "--port #{port} "\
           "--file #{file} "\
           "--sock #{ctrl} "\
           "#{acl.join(' ')}"
      exec(cmd)
    end
  end
  
  def kill
    Process.kill("INT", @pid)
    Process.wait(@pid)
  end
  
  def read(offset, length)
    IO.popen("#{@bin} read "\
             "--addr #{ip} "\
             "--port #{port} "\
             "--from #{offset} "\
             "--size #{length}","r") do |fh|
      return fh.read
    end
    raise "read failed" unless $?.success?
  end
  
  def write(offset, data)
    IO.popen("#{@bin} write "\
             "--addr #{ip} "\
             "--port #{port} "\
             "--from #{offset} "\
             "--size #{data.length}","w") do |fh|
      fh.write(data)
    end
    raise "write failed" unless $?.success?
    nil
  end
  
  def mirror(bandwidth=nil, action=nil)
    control_command("mirror", ip, port, bandwidth, action)
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
