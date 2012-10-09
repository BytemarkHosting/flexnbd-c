require 'socket'
require 'thread'
require 'open3'
require 'timeout'
require 'rexml/document'
require 'rexml/streamlistener'

Thread.abort_on_exception = true


class Executor
  attr_reader :pid

  def run( cmd )
    @pid = fork do exec cmd end
  end
end # class Executor


class ValgrindExecutor
  attr_reader :pid

  def run( cmd )
    @pid = fork do exec "valgrind --track-origins=yes #{cmd}" end
  end
end # class ValgrindExecutor


class ValgrindKillingExecutor
  attr_reader :pid

  class Error
    attr_accessor :what, :kind, :pid
    attr_reader :backtrace
    def initialize
      @backtrace=[]
      @what = ""
      @kind = ""
      @pid = ""
    end

    def add_frame
      @backtrace << {}
    end

    def add_fn(fn)
      @backtrace.last[:fn] = fn
    end

    def add_file(file)
      @backtrace.last[:file] = file
    end

    def add_line(line)
      @backtrace.last[:line] = line
    end

    def to_s
      ([@what + " (#{@kind}) in #{@pid}"] + @backtrace.map{|h| "#{h[:file]}:#{h[:line]} #{h[:fn]}" }).join("\n")
    end

  end # class Error


  class ErrorListener
    include REXML::StreamListener
    def initialize( killer )
      @killer = killer
      @error = Error.new
      @found = false
    end

    def text( text )
      @text = text
    end

    def tag_start(tag, attrs)
      case tag.to_s
      when "error"
        @found = true
      when "frame"
        @error.add_frame
      end
    end

    def tag_end(tag)
      case tag.to_s
      when "what"
        @error.what = @text if @found
        @text = ""
      when "kind"
        @error.kind = @text if @found
      when "file"
        @error.add_file( @text ) if @found
      when "fn"
        @error.add_fn( @text ) if @found
      when "line"
        @error.add_line( @text ) if @found
      when "error", "stack"
        @killer.call( @error )
      when "pid"
        @error.pid=@text
      end
    end
  end # class ErrorListener


  class DebugErrorListener < ErrorListener
    def text( txt )
      print txt
      super( txt )
    end

    def tag_start( tag, attrs )
      print "<#{tag}>"
      super( tag, attrs )
    end

    def tag_end( tag )
      print "</#{tag}>"
      super( tag )
    end
  end


  def initialize
    @pid = nil
  end

  def run( cmd )
    @io_r, io_w = IO.pipe
    @pid = fork do exec( "valgrind --xml=yes --xml-fd=#{io_w.fileno} " + cmd ) end
    launch_watch_thread( @pid, @io_r )
    @pid
  end


  def call( err )
    $stderr.puts "*"*72
    $stderr.puts "* Valgrind error spotted:"
    $stderr.puts err.to_s.split("\n").map{|s| "  #{s}"}
    $stderr.puts "*"*72
    Process.kill( "KILL", @pid )
    exit(1)
  end


  private

  def pick_listener
    ENV['DEBUG'] ? DebugErrorListener : ErrorListener
  end

  def launch_watch_thread(pid, io_r)
    Thread.start do
      io_source = REXML::IOSource.new( io_r )
      listener = pick_listener.new( self )
      REXML::Document.parse_stream( io_source, listener )
    end
  end


end # class ValgrindExecutor


module FlexNBD
  # Noddy test class to exercise FlexNBD from the outside for testing.
  #
  class FlexNBD
    attr_reader :bin, :ctrl, :pid, :ip, :port

    class << self
      def counter
        Dir['tmp/*'].select{|f| File.file?(f)}.length + 1
      end
    end

    def pick_executor
      kls = if ENV['VALGRIND']
              if ENV['VALGRIND'] =~ /kill/
                ValgrindKillingExecutor
              else
                ValgrindExecutor
              end
            else
              Executor
            end
    end


    def build_debug_opt
      if @do_debug
        "--verbose"
      else
        "--quiet"
      end
    end

    def initialize( bin, ip, port )
      @bin  = bin
      @do_debug = ENV['DEBUG']
      @debug = build_debug_opt
      raise "#{bin} not executable" unless File.executable?(bin)
      @executor = pick_executor.new
      @ctrl = "/tmp/.flexnbd.ctrl.#{Time.now.to_i}.#{rand}"
      @ip = ip
      @port = port
      @kill = []
    end


    def debug?
      !!@do_debug
    end

    def debug( msg )
      $stderr.puts msg if debug?
    end


    def serve_cmd( file, acl )
      "#{bin} serve "\
        "--addr #{ip} "\
        "--port #{port} "\
        "--file #{file} "\
        "--sock #{ctrl} "\
        "#{@debug} "\
        "#{acl.join(' ')}"
    end


    def listen_cmd( file, acl )
      "#{bin} listen "\
        "--addr #{ip} "\
        "--port #{port} "\
        "--file #{file} "\
        "--sock #{ctrl} "\
        "#{@debug} "\
        "#{acl.join(' ')}"
    end


    def read_cmd( offset, length )
      "#{bin} read "\
        "--addr #{ip} "\
        "--port #{port} "\
        "--from #{offset} "\
        "#{@debug} "\
        "--size #{length}"
    end


    def write_cmd( offset, data )
      "#{bin} write "\
        "--addr #{ip} "\
        "--port #{port} "\
        "--from #{offset} "\
        "#{@debug} "\
        "--size #{data.length}"
    end


    def base_mirror_opts( dest_ip, dest_port )
      "--addr #{dest_ip} "\
        "--port #{dest_port} "\
        "--sock #{ctrl} "\
    end

    def unlink_mirror_opts( dest_ip, dest_port )
      "#{base_mirror_opts( dest_ip, dest_port )} "\
        "--unlink "
    end

    def base_mirror_cmd( opts )
      "#{@bin} mirror "\
        "#{opts} "\
        "#{@debug}"
    end

    def mirror_cmd(dest_ip, dest_port)
      base_mirror_cmd( base_mirror_opts( dest_ip, dest_port ) )
    end

    def mirror_unlink_cmd( dest_ip, dest_port )
      base_mirror_cmd( unlink_mirror_opts( dest_ip, dest_port ) )
    end

    def break_cmd
      "#{@bin} break "\
        "--sock #{ctrl} "\
        "#{@debug}"
    end

    def status_cmd
      "#{@bin} status "\
        "--sock #{ctrl} "\
        "#{@debug}"
    end

    def acl_cmd( *acl )
      "#{@bin} acl " \
        "--sock #{ctrl} "\
        "#{@debug} "\
        "#{acl.join " "}"
    end


    def run_serve_cmd(cmd)
      File.unlink(ctrl) if File.exists?(ctrl)
      debug( cmd )

      @pid = @executor.run( cmd )

      while !File.socket?(ctrl)
        pid, status = Process.wait2(@pid, Process::WNOHANG)
        raise "server did not start (#{cmd})" if pid
        sleep 0.1
      end

      start_wait_thread( @pid )
      at_exit { kill }
    end
    private :run_serve_cmd


    def serve( file, *acl)
      cmd = serve_cmd( file, acl )
      run_serve_cmd( cmd )
    end

    def listen(file, *acl)
      run_serve_cmd( listen_cmd( file, acl ) )
    end


    def start_wait_thread( pid )
      @wait_thread = Thread.start do
        _, status = Process.waitpid2( pid )

        if @kill
          if status.signaled?
            fail "flexnbd quit with a bad signal: #{status.inspect}" unless
            @kill.include? status.termsig
          else
            fail "flexnbd quit with a bad status: #{status.inspect}" unless
            @kill.include? status.exitstatus
          end
        else
          $stderr.puts "flexnbd #{self.pid} quit"
          fail "flexnbd #{self.pid} quit early with status #{status.to_i}"
        end
      end
    end


    def can_die(*status)
      status = [0] if status.empty?
      @kill += status
    end

    def kill
      # At this point, to a certain degree we don't care what the exit
      # status is
      can_die(1)
      if @pid
        begin
          Process.kill("INT", @pid)
        rescue Errno::ESRCH => e
          # already dead.  Presumably this means it went away after a
          # can_die() call.
        end
      end
      @wait_thread.join if @wait_thread
    end

    def read(offset, length)
      cmd = read_cmd( offset, length )
      debug( cmd )

      IO.popen(cmd) do |fh|
        return fh.read
      end
      raise IOError.new "NBD read failed" unless $?.success?
      out
    end

    def write(offset, data)
      cmd = write_cmd( offset, data )
      debug( cmd )

      IO.popen(cmd, "w") do |fh|
        fh.write(data)
      end
      raise IOError.new "NBD write failed" unless $?.success?
      nil
    end


    def join
      @wait_thread.join
    end


    def mirror_unchecked( dest_ip, dest_port, bandwidth=nil, action=nil, timeout=nil )
      cmd = mirror_cmd( dest_ip, dest_port)
      debug( cmd )

      maybe_timeout( cmd, timeout )
    end


    def mirror_unlink( dest_ip, dest_port, timeout=nil )
      cmd = mirror_unlink_cmd( dest_ip, dest_port )
      debug( cmd )

      maybe_timeout( cmd, timeout )
    end


    def maybe_timeout(cmd, timeout=nil )
      stdout, stderr = "",""
      run = Proc.new do
        Open3.popen3( cmd ) do |io_in, io_out, io_err|
          io_in.close
          stdout.replace io_out.read
          stderr.replace io_err.read
        end
      end

      if timeout
        Timeout.timeout(timeout, &run)
      else
        run.call
      end

      [stdout, stderr]
    end


    def mirror(dest_ip, dest_port, bandwidth=nil, action=nil)
      stdout, stderr = mirror_unchecked( dest_ip, dest_port, bandwidth, action )
      raise IOError.new( "Migrate command failed\n" + stderr) unless $?.success?

      stdout
    end



    def break(timeout=nil)
      cmd = break_cmd
      debug( cmd )

      maybe_timeout( cmd, timeout )
    end


    def acl(*acl)
      cmd = acl_cmd( *acl )
      debug( cmd )

      maybe_timeout( cmd, 2 )
    end


    def status( timeout = nil )
      cmd = status_cmd()
      debug( cmd )

      o,e = maybe_timeout( cmd, timeout )

      [parse_status(o), e]
    end


    def launched?
      !!@pid
    end


    def paused
      Process.kill( "STOP", @pid )
      yield
    ensure
      Process.kill( "CONT", @pid )
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


    def parse_status( status )
      hsh = {}

      status.split(" ").each do |part|
        next if part.strip.empty?
        a,b = part.split("=")
        b.strip!
        b = true if b == "true"
        b = false if b == "false"

        hsh[a.strip] = b
      end

      hsh
    end


  end

end
