# encoding: utf-8

require 'flexnbd'
require 'file_writer'

class Environment
  attr_reader( :blocksize, :filename1, :filename2, :ip,
               :port1, :port2, :nbd1, :nbd2, :file1, :file2, :rebind_port1 )

  def initialize
    @blocksize = 1024
    @filename1 = "/tmp/.flexnbd.test.#{$$}.#{Time.now.to_i}.1"
    @filename2 = "/tmp/.flexnbd.test.#{$$}.#{Time.now.to_i}.2"
    @ip = "127.0.0.1"
    @available_ports = [*40000..41000] - listening_ports
    @port1 = @available_ports.shift
    @rebind_port1 = @available_ports.shift
    @port2 = @available_ports.shift
    @rebind_port2 = @available_ports.shift
    @nbd1 = FlexNBD.new("../../build/flexnbd", @ip, @port1, @ip, @rebind_port1)
    @nbd2 = FlexNBD.new("../../build/flexnbd", @ip, @port2, @ip, @rebind_port2)

    @fake_pid = nil
  end


  def serve1(*acl)
    @nbd1.serve(@filename1, *acl)
  end

  def serve2(*acl)
    @nbd2.serve(@filename2, *acl)
  end


  def listen1( *acl )
    @nbd1.listen( @filename1, *(acl.empty? ? @acl1: acl) )
  end

  def listen2( *acl )
    @nbd2.listen( @filename2, *acl )
  end


  def acl1( *acl )
    @nbd1.acl( *acl )
  end

  def acl2( *acl )
    @nbd2.acl( *acl )
  end


  def status1
    @nbd1.status.first
  end

  def status2
    @nbd2.status.first
  end



  def mirror12
    @nbd1.mirror( @nbd2.ip, @nbd2.port )
  end

  def mirror12_unchecked
    @nbd1.mirror_unchecked( @nbd2.ip, @nbd2.port, nil, nil, 10 )
  end


  def writefile1(data)
    @file1 = FileWriter.new(@filename1, @blocksize).write(data)
  end

  def writefile2(data)
    @file2 = FileWriter.new(@filename2, @blocksize).write(data)
  end


  def truncate1( size )
    system "truncate -s #{size} #{@filename1}"
  end


  def listening_ports
    `netstat -ltn`.
      split("\n").
      map { |x| x.split(/\s+/) }[2..-1].
      map { |l| l[3].split(":")[-1].to_i }
  end


  def cleanup
    if @fake_pid
      begin
        Process.waitpid2( @fake_pid )
      rescue Errno::ESRCH
      end
    end


    @nbd1.can_die(0)
    @nbd1.kill
    @nbd2.kill

    [@filename1, @filename2].each do |f|
      File.unlink(f) if File.exists?(f)
    end
  end


  def run_fake( name, addr, port, rebind_addr = addr, rebind_port = port )
    fakedir = File.join( File.dirname( __FILE__ ), "fakes" )
    fake = Dir[File.join( fakedir, name ) + "*"].sort.find { |fn|
      File.executable?( fn )
    }

    raise "no fake executable" unless fake
    raise "no addr" unless addr
    raise "no port" unless port
    raise "no rebind_addr" unless rebind_addr
    raise "no rebind_port" unless rebind_port

    @fake_pid = fork do
      exec [fake, addr, port, @nbd1.pid, rebind_addr, rebind_port].map{|x| x.to_s}.join(" ")
    end
    sleep(0.5)
  end


  def fake_reports_success
    _,status = Process.waitpid2( @fake_pid )
    @fake_pid = nil
    status.success?
  end


end # class Environment

