#!/usr/bin/env ruby

require 'test/unit'
require 'flexnbd/fake_source'
require 'socket'
require 'fileutils'
require 'tmpdir'

Thread.abort_on_exception = true

class TestWriteDuringMigration < Test::Unit::TestCase
  def setup
    @flexnbd = File.expand_path('../../build/flexnbd')

    raise 'No binary!' unless File.executable?(@flexnbd)

    @size = 20 * 1024 * 1024 # 20MB
    @write_data = 'foo!' * 2048 # 8K write
    @source_port = 9990
    @dest_port = 9991
    @source_sock = 'src.sock'
    @dest_sock = 'dst.sock'
    @source_file = 'src.file'
    @dest_file = 'dst.file'
  end

  def teardown
    [@dst_proc, @src_proc].each do |pid|
      next unless pid
      begin
        Process.kill('KILL', pid)
      rescue StandardError
        nil
      end
    end
  end

  def debug_arg
    ENV['DEBUG'] ? '--verbose' : ''
  end

  def launch_servers
    @dst_proc = fork do
      cmd = "#{@flexnbd} listen -l 127.0.0.1 -p #{@dest_port} -f #{@dest_file} -s #{@dest_sock} #{debug_arg}"
      exec cmd
    end

    @src_proc = fork do
      cmd = "#{@flexnbd} serve -l 127.0.0.1 -p #{@source_port} -f #{@source_file} -s #{@source_sock} #{debug_arg}"
      exec cmd
    end
    begin
      awaiting = nil
      Timeout.timeout(10) do
        awaiting = :source
        sleep 0.1 until File.exist?(@source_sock)
        awaiting = :dest
        sleep 0.1 until File.exist?(@dest_sock)
      end
    rescue Timeout::Error
      case awaiting
      when :source
        raise "Couldn't get a source socket."
      when :dest
        raise "Couldn't get a destination socket."
      else
        raise "Something went wrong I don't understand."
      end
    end
  end

  def make_files
    FileUtils.touch(@source_file)
    File.truncate(@source_file, @size)
    FileUtils.touch(@dest_file)
    File.truncate(@dest_file, @size)

    File.open(@source_file, 'wb') { |f| f.write 'a' * @size }
  end

  def start_mirror
    UNIXSocket.open(@source_sock) do |sock|
      sock.write(['mirror', '127.0.0.1', @dest_port.to_s, 'exit'].join("\x0A") + "\x0A\x0A")
      sock.flush
      sock.readline
    end
  end

  def wait_for_quit
    Timeout.timeout(10) do
      Process.waitpid2(@dst_proc)
      Process.waitpid2(@src_proc)
    end
  end

  def source_writer
    client = FlexNBD::FakeSource.new('127.0.0.1', @source_port, 'Timed out connecting')
    offsets = Range.new(0, (@size - @write_data.size) / 4096).to_a
    loop do
      begin
        client.write(offsets[rand(offsets.size)] * 4096, @write_data)
      rescue StandardError
        # We expect a broken write at some point, so ignore it
        break
      end
    end
  end

  def bombard_with_status
    loop do
      begin
        UNIXSocket.open(@source_sock) do |sock|
          sock.write("status\x0A\x0A")
          sock.flush
          sock.readline
        end
      rescue Errno::ENOENT
        # If the socket disappears, that's OK.
        break
      end
    end
  end

  def assert_both_sides_identical
    # puts `md5sum #{@source_file} #{@dest_file}`

    # Ensure each block matches
    File.open(@source_file, 'r') do |source|
      File.open(@dest_file, 'r') do |dest|
        0.upto(@size / 4096) do |block_num|
          s_data = source.read(4096)
          d_data = dest.read(4096)

          assert s_data == d_data, "Block #{block_num} mismatch!"

          source.seek(4096, IO::SEEK_CUR)
          dest.seek(4096, IO::SEEK_CUR)
        end
      end
    end
  end

  def test_write_during_migration
    Dir.mktmpdir do |tmpdir|
      Dir.chdir(tmpdir) do
        make_files

        launch_servers

        src_writer = Thread.new { source_writer }

        start_mirror
        wait_for_quit
        src_writer.join
        assert_both_sides_identical
      end
    end
  end

  def test_many_clients_during_migration
    Dir.mktmpdir do |tmpdir|
      Dir.chdir(tmpdir) do
        make_files

        launch_servers

        src_writers_1 = (1..5).collect { Thread.new { source_writer } }

        start_mirror

        src_writers_2 = (1..5).collect { Thread.new { source_writer } }

        wait_for_quit
        (src_writers_1 + src_writers_2).each(&:join)
        assert_both_sides_identical
      end
    end
  end


  def test_status_call_after_cleanup
    Dir.mktmpdir do |tmpdir|
      Dir.chdir(tmpdir) do
        make_files

        launch_servers

        status_poker = Thread.new { bombard_with_status }

        start_mirror

        wait_for_quit
        status_poker.join
        assert_both_sides_identical
      end
    end
  end
end
