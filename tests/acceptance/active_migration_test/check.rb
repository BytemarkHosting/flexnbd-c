#!/usr/bin/env ruby
require 'rubygems'
require 'flexnbd/fake_source'
require 'socket'
require 'fileutils'


FLEXNBD = ARGV.shift
raise "No binary!" unless File.executable?( FLEXNBD )
raise "No trickle!" unless system "which trickle > /dev/null"

Thread.abort_on_exception = true


#BREAKIT = false
BREAKIT = true

MB = 1024*1024
GB = 1024*MB
SIZE = 20*MB 
WRITE_DATA = "foo!" * 2048 # 8K write
SOURCE_PORT = 9990
DEST_PORT = 9991
SOURCE_SOCK = "src.sock"
DEST_SOCK = "dst.sock"
SOURCE_FILE = "src.file"
DEST_FILE = "dst.file"

puts "Cleaning up from old runs..."
FileUtils.rm_f "src*"
FileUtils.rm_f "dst*"

puts "Creating source & destination files..."
FileUtils.touch(SOURCE_FILE)
File.truncate(SOURCE_FILE, SIZE)
FileUtils.touch(DEST_FILE)
File.truncate(DEST_FILE, SIZE)

puts "Making source data"
File.open(SOURCE_FILE, "wb"){|f| f.write "a"*SIZE }
#Kernel.system "dd if=/dev/urandom of=#{SOURCE_FILE} bs=#{SIZE} count=1"

puts "Starting destination process..."
dst_proc = fork() {
  cmd = "#{FLEXNBD} listen -l 127.0.0.1 -p #{DEST_PORT} -f #{DEST_FILE} -s #{DEST_SOCK} -v"
  exec cmd
}

puts "Starting source process..."
src_proc = fork() {
  cmd = "#{FLEXNBD} serve -l 127.0.0.1 -p #{SOURCE_PORT} -f #{SOURCE_FILE} -s #{SOURCE_SOCK} -v"
  #exec "trickle -t 1 -u 5120 -s #{cmd}"
  exec cmd
}

puts "dst_proc is #{dst_proc}"
puts "src_proc is #{src_proc}"


at_exit { 
  [dst_proc, src_proc].each do |pid|
    Process.kill( "KILL", pid ) rescue nil
  end
  [SOURCE_FILE, DEST_FILE, SOURCE_SOCK, DEST_SOCK].each do |filename|
    FileUtils.rm_f filename
  end
}



puts "Sleeping to let flexnbds run..."
Timeout.timeout(10) do
  sleep 0.1 while !File.exists?( SOURCE_SOCK )
  puts "Got source sock"
  sleep 0.1 while !File.exists?( DEST_SOCK )
  puts "Got dest sock"
end


if BREAKIT 
  # Start writing to the source
  puts "Creating thread to write to the source..."

  src_writer = Thread.new do
    client = FlexNBD::FakeSource.new( "127.0.0.1", SOURCE_PORT, "Timed out connecting" )
    loop do
      begin
        client.write(0, WRITE_DATA)
      rescue => err
        puts "Writer encountered #{err.inspect} writing to source"
        break
      end
    end
  end
end

puts "Starting mirroring process..."
UNIXSocket.open(SOURCE_SOCK) {|sock|
  sock.write(["mirror", "127.0.0.1", DEST_PORT.to_s, "unlink"].join("\x0A") + "\x0A\x0A")
  sock.flush
  rsp = sock.readline
  puts "Response: #{rsp}"
}

# tell serve to migrate to the listen one

Timeout.timeout( 10 ) do
  start_time =  Time.now
  puts "Waiting for destination #{dst_proc} to exit..."
  dst_result = Process::waitpid2(dst_proc)
  puts "destination exited: #{dst_result.inspect}"

  puts "Waiting for source to exit..."
  src_result = Process::waitpid2(src_proc)
  puts "Source exited after #{Time.now - start_time} seconds: #{src_result.inspect}"
end


if BREAKIT
  puts "Waiting for writer to die..."
  src_writer.join
  puts "Writer has died"
end

