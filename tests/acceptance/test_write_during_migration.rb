#!/usr/bin/env ruby

require 'test/unit'
require 'flexnbd/fake_source'
require 'socket'
require 'fileutils'
require 'tmpdir'

Thread.abort_on_exception = true

class TestWriteDuringMigration < Test::Unit::TestCase

  def setup
    @flexnbd = File.expand_path("../../build/flexnbd")

    raise "No binary!" unless File.executable?( @flexnbd )
    raise "No trickle!" unless system "which trickle > /dev/null"


    @size = 20*1024*1024 # 20MB
    @write_data = "foo!" * 2048 # 8K write
    @source_port = 9990
    @dest_port = 9991
    @source_sock = "src.sock"
    @dest_sock = "dst.sock"
    @source_file = "src.file"
    @dest_file = "dst.file"
  end


  def teardown
    [@dst_proc, @src_proc].each do |pid|
      if pid
        Process.kill( "KILL", pid ) rescue nil
      end
    end
  end


  def launch_servers
    @dst_proc = fork() {
      cmd = "#{@flexnbd} listen -l 127.0.0.1 -p #{@dest_port} -f #{@dest_file} -s #{@dest_sock}"
      exec cmd
    }

    @src_proc = fork() {
      cmd = "#{@flexnbd} serve -l 127.0.0.1 -p #{@source_port} -f #{@source_file} -s #{@source_sock}"
      exec cmd
    }
    begin
      awaiting = nil
      Timeout.timeout(10) do
        awaiting = :source
        sleep 0.1 while !File.exists?( @source_sock ) 
        awaiting = :dest
        sleep 0.1 while !File.exists?( @dest_sock )
      end
    rescue Timeout::Error
      case awaiting
      when :source
        fail "Couldn't get a source socket."
      when :dest
        fail "Couldn't get a destination socket."
      else
        fail "Something went wrong I don't understand."
      end
    end
  end


  def make_files()
    FileUtils.touch(@source_file)
    File.truncate(@source_file, @size)
    FileUtils.touch(@dest_file)
    File.truncate(@dest_file, @size)

    File.open(@source_file, "wb"){|f| f.write "a"*@size }
  end


  def start_mirror
    UNIXSocket.open(@source_sock) {|sock|
      sock.write(["mirror", "127.0.0.1", @dest_port.to_s, "unlink"].join("\x0A") + "\x0A\x0A")
      sock.flush
      rsp = sock.readline
    }
  end


  def wait_for_quit()
    Timeout.timeout( 10 ) do
      start_time =  Time.now
      dst_result = Process::waitpid2(@dst_proc)
      src_result = Process::waitpid2(@src_proc)
    end

  end


  def test_write_during_migration

    Dir.mktmpdir() do |tmpdir|
      Dir.chdir( tmpdir ) do
        make_files()
        
        launch_servers()

        src_writer = Thread.new do
          client = FlexNBD::FakeSource.new( "127.0.0.1", @source_port, "Timed out connecting" )
          loop do
            begin
              client.write(0, @write_data)
            rescue => err
              # We expect a broken write at some point, so ignore it
              break
            end
          end
        end

        start_mirror()
        wait_for_quit()
        src_writer.join
      end
    end
  end


end
