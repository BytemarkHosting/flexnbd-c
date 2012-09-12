#!/usr/bin/env ruby
require 'rubygems'
require 'linux/nbd_client'
require 'socket'
require 'fileutils'

Thread.abort_on_exception = true

SIZE = 1024*1024*1024*10 # 10G
WRITE_DATA = "foo!" * 2048 # 8K write

puts "Cleaning up from old runs..."
FileUtils.rm_f "src*"
FileUtils.rm_f "dst*"

puts "Creating source & destination files..."
FileUtils.touch("src.file")
File.truncate("src.file", SIZE)
FileUtils.touch("dst.file")
File.truncate("dst.file", SIZE)

puts "Making filesystem on source..."
result = Kernel.system "/sbin/mkfs.ext3 -F src.file" # some data to start with
puts "Result of making filesystem: #{result.inspect}"

puts "Starting destination process..."
dst_proc = fork() {
  exec("flexnbd listen -l 127.0.0.1 -p 9991 -f dst.file -s dst.sock -v >dst.stdout 2>dst.stderr")
}

puts "Starting source process..."
src_proc = fork() {
  exec("flexnbd serve -l 127.0.0.1 -p 9990 -f src.file -s src.sock -v >src.stdout 2>src.stderr")
}

puts "Sleeping to let flexnbds run..."
sleep 10



# Start writing to the source
puts "Creating thread to write to the source..."
src_writer = Thread.new do
  client = Linux::NbdClient.new("127.0.0.1", 9990, true)
  loop do
    begin
      client.write(0, WRITE_DATA)
    rescue => err
      puts "Writer encountered #{err.inspect} writing to source"
      break
    end
  end
end

puts "Starting mirroring process..."
UNIXSocket.open("src.sock") {|sock|
  sock.write(["mirror", "127.0.0.1", "9991", "unlink"].join("\x0A") + "\x0A\x0A")
  sock.flush
  rsp = sock.readline
  puts "Response: #{rsp}"
}

# tell serve to migrate to the listen one

puts "Waiting for destination to exit..."
dst_result = Process::waitpid2(dst_proc)
puts "destination exited: #{dst_result.inspect}"

puts "Waiting for source to exit..."
src_result = Process::waitpid2(src_proc)
puts "Source exited: #{src_result.inspect}"

puts "Waiting for writer to die..."
src_writer.join
puts "Writer has died"

